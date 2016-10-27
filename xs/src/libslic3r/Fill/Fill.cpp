#include <stdio.h>

#include "../ClipperUtils.hpp"
#include "../Surface.hpp"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

#if 0
// Generate infills for Slic3r::Layer::Region.
// The Slic3r::Layer::Region at this point of time may contain
// surfaces of various types (internal/bridge/top/bottom/solid).
// The infills are generated on the groups of surfaces with a compatible type. 
// Returns an array of Slic3r::ExtrusionPath::Collection objects containing the infills generaed now
// and the thin fills generated by generate_perimeters().
void make_fill(LayerRegion &layerm, ExtrusionEntityCollection &out)
{    
//    Slic3r::debugf "Filling layer %d:\n", $layerm->layer->id;
    
    double  fill_density           = layerm.region()->config.fill_density;
    Flow    infill_flow            = layerm.flow(frInfill);
    Flow    solid_infill_flow      = layerm.flow(frSolidInfill);
    Flow    top_solid_infill_flow  = layerm.flow(frTopSolidInfill);

    Surfaces surfaces;
    
    // merge adjacent surfaces
    // in case of bridge surfaces, the ones with defined angle will be attached to the ones
    // without any angle (shouldn't this logic be moved to process_external_surfaces()?)
    {
        SurfacesPtr surfaces_with_bridge_angle;
        surfaces_with_bridge_angle.reserve(layerm->fill_surfaces.surfaces.size());
        for (Surfaces::iterator it = layerm->fill_surfaces.surfaces.begin(); it != layerm->fill_surfaces.surfaces.end(); ++ it)
            if (it->bridge_angle >= 0)
                surfaces_with_bridge_angle.push_back(&(*it));
        
        // group surfaces by distinct properties (equal surface_type, thickness, thickness_layers, bridge_angle)
        // group is of type Slic3r::SurfaceCollection
        //FIXME: Use some smart heuristics to merge similar surfaces to eliminate tiny regions.
        std::vector<SurfacesPtr> groups;
        layerm->fill_surfaces.group(&groups);
        
        // merge compatible groups (we can generate continuous infill for them)
        {
            // cache flow widths and patterns used for all solid groups
            // (we'll use them for comparing compatible groups)
            my @is_solid = my @fw = my @pattern = ();
            for (my $i = 0; $i <= $num_ groups; $i++) {
                // we can only merge solid non-bridge surfaces, so discard
                // non-solid surfaces
                if ($groups[$i][0]->is_solid && (!$groups[$i][0]->is_bridge || $layerm->layer->id == 0)) {
                    $is_solid[$i] = 1;
                    $fw[$i] = ($groups[$i][0]->surface_type == S_TYPE_TOP)
                        ? $top_solid_infill_flow->width
                        : $solid_infill_flow->width;
                    $pattern[$i] = $groups[$i][0]->is_external
                        ? $layerm->region->config->external_fill_pattern
                        : 'rectilinear';
                } else {
                    $is_solid[$i]   = 0;
                    $fw[$i]         = 0;
                    $pattern[$i]    = 'none';
                }
            }
            
            // loop through solid groups
            for (my $i = 0; $i <= $num_groups; $i++) {
                next if !$is_solid[$i];
                
                // find compatible groups and append them to this one
                for (my $j = $i+1; $j <= $num_groups; $j++) {
                    next if !$is_solid[$j];
                
                    if ($fw[$i] == $fw[$j] && $pattern[$i] eq $pattern[$j]) {
                        // groups are compatible, merge them
                        push @{$groups[$i]}, @{$groups[$j]};
                        splice @groups,     $j, 1;
                        splice @is_solid,   $j, 1;
                        splice @fw,         $j, 1;
                        splice @pattern,    $j, 1;
                    }
                }
            }
        }
        
        // give priority to bridges
        @groups = sort { ($a->[0]->bridge_angle >= 0) ? -1 : 0 } @groups;
        
        foreach my $group (@groups) {
            // Make a union of polygons defining the infiill regions of a group, use a safety offset.
            my $union_p = union([ map $_->p, @$group ], 1);
            
            // Subtract surfaces having a defined bridge_angle from any other, use a safety offset.
            if (@surfaces_with_bridge_angle && $group->[0]->bridge_angle < 0) {
                $union_p = diff(
                    $union_p,
                    [ map $_->p, @surfaces_with_bridge_angle ],
                    1,
                );
            }
            
            // subtract any other surface already processed
            //FIXME Vojtech: Because the bridge surfaces came first, they are subtracted twice!
            my $union = diff_ex(
                $union_p,
                [ map $_->p, @surfaces ],
                1,
            );
            
            push @surfaces, map $group->[0]->clone(expolygon => $_), @$union;
        }
    }
    
    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    {
        my $distance_between_surfaces = max(
            $infill_flow->scaled_spacing,
            $solid_infill_flow->scaled_spacing,
            $top_solid_infill_flow->scaled_spacing,
        );
        my $collapsed = diff(
            [ map @{$_->expolygon}, @surfaces ],
            offset2([ map @{$_->expolygon}, @surfaces ], -$distance_between_surfaces/2, +$distance_between_surfaces/2),
            1,
        );
        push @surfaces, map Slic3r::Surface->new(
            expolygon       => $_,
            surface_type    => S_TYPE_INTERNALSOLID,
        ), @{intersection_ex(
            offset($collapsed, $distance_between_surfaces),
            [
                (map @{$_->expolygon}, grep $_->surface_type == S_TYPE_INTERNALVOID, @surfaces),
                (@$collapsed),
            ],
            1,
        )};
    }
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("fill_" . $layerm->print_z . ".svg",
            expolygons      => [ map $_->expolygon, grep !$_->is_solid, @surfaces ],
            red_expolygons  => [ map $_->expolygon, grep  $_->is_solid, @surfaces ],
        );
    }
    
    SURFACE: foreach my $surface (@surfaces) {
        next if $surface->surface_type == S_TYPE_INTERNALVOID;
        my $filler          = $layerm->region->config->fill_pattern;
        my $density         = $fill_density;
        my $role = ($surface->surface_type == S_TYPE_TOP) ? FLOW_ROLE_TOP_SOLID_INFILL
            : $surface->is_solid ? FLOW_ROLE_SOLID_INFILL
            : FLOW_ROLE_INFILL;
        my $is_bridge       = $layerm->layer->id > 0 && $surface->is_bridge;
        my $is_solid        = $surface->is_solid;
        
        if ($surface->is_solid) {
            $density = 100;
            $filler = 'rectilinear';
            if ($surface->is_external && !$is_bridge) {
                $filler = $layerm->region->config->external_fill_pattern;
            }
        } else {
            next SURFACE unless $density > 0;
        }
        
        // get filler object
        my $f = $self->filler($filler);
        
        // calculate the actual flow we'll be using for this infill
        my $h = $surface->thickness == -1 ? $layerm->layer->height : $surface->thickness;
        my $flow = $layerm->region->flow(
            $role,
            $h,
            $is_bridge || $f->use_bridge_flow,
            $layerm->layer->id == 0,
            -1,
            $layerm->layer->object,
        );
        
        // calculate flow spacing for infill pattern generation
        my $using_internal_flow = 0;
        if (!$is_solid && !$is_bridge) {
            // it's internal infill, so we can calculate a generic flow spacing 
            // for all layers, for avoiding the ugly effect of
            // misaligned infill on first layer because of different extrusion width and
            // layer height
            my $internal_flow = $layerm->region->flow(
                FLOW_ROLE_INFILL,
                $layerm->layer->object->config->layer_height,  // TODO: handle infill_every_layers?
                0,  // no bridge
                0,  // no first layer
                -1, // auto width
                $layerm->layer->object,
            );
            $f->set_spacing($internal_flow->spacing);
            $using_internal_flow = 1;
        } else {
            $f->set_spacing($flow->spacing);
        }

        my $link_max_length = 0;
        if (! $is_bridge) {
            $link_max_length = $layerm->region->config->get_abs_value_over($surface->is_external ? 'external_fill_link_max_length' : 'fill_link_max_length', $flow->spacing);
            print "flow spacing: ", $flow->spacing, " is_external: ", $surface->is_external, ", link_max_length: $link_max_length\n";
        }
        
        $f->set_layer_id($layerm->layer->id);
        $f->set_z($layerm->layer->print_z);
        $f->set_angle(deg2rad($layerm->region->config->fill_angle));
        // Maximum length of the perimeter segment linking two infill lines.
        $f->set_link_max_length(scale($link_max_length));
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        $f->set_loop_clipping(scale($flow->nozzle_diameter) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);
        
        // apply half spacing using this flow's own spacing and generate infill
        my @polylines = $f->fill_surface(
            $surface,
            density         => $density/100,
            layer_height    => $h,
        );
        next unless @polylines;

        
        // calculate actual flow from spacing (which might have been adjusted by the infill
        // pattern generator)
        if ($using_internal_flow) {
            // if we used the internal flow we're not doing a solid infill
            // so we can safely ignore the slight variation that might have
            // been applied to $f->flow_spacing
        } else {
            $flow = Slic3r::Flow->new_from_spacing(
                spacing         => $f->spacing,
                nozzle_diameter => $flow->nozzle_diameter,
                layer_height    => $h,
                bridge          => $is_bridge || $f->use_bridge_flow,
            );
        }

        // save into layer
        {
            my $role = $is_bridge ? EXTR_ROLE_BRIDGE
                : $is_solid ? (($surface->surface_type == S_TYPE_TOP) ? EXTR_ROLE_TOPSOLIDFILL : EXTR_ROLE_SOLIDFILL)
                : EXTR_ROLE_FILL;
            
            out.
            push @fills, my $collection = Slic3r::ExtrusionPath::Collection->new;
            // Only concentric fills are not sorted.
            $collection->no_sort($f->no_sort);
            $collection->append(
                map Slic3r::ExtrusionPath->new(
                    polyline    => $_,
                    role        => $role,
                    mm3_per_mm  => $flow->mm3_per_mm,
                    width       => $flow->width,
                    height      => $flow->height,
                ), map @$_, @polylines,
            );
        }
    }
    
    // add thin fill regions
    // thin_fills are of C++ Slic3r::ExtrusionEntityCollection, perl type Slic3r::ExtrusionPath::Collection
    // Unpacks the collection, creates multiple collections per path.
    // The path type could be ExtrusionPath, ExtrusionLoop or ExtrusionEntityCollection.
    // Why the paths are unpacked?
    for (ExtrusionEntitiesPtr::iterator thin_fill = layerm.thin_fills.entities.begin(); thin_fill != layerm.thin_fills.entities.end(); ++ thin_fill) {
        // ExtrusionEntityCollection
        out.append(new ExtrusionEntityCollection->new($thin_fill);
    }
    
    return @fills;
}
#endif

} // namespace Slic3r
