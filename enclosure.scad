// Basic parametric box enclosure
// Units are millimeters (OpenSCAD convention)

// --- Parameters ---
esp_long_axis = 23;
esp_short_axis = 18;
wall    = 2;    // wall thickness
usb_body_len    = 21;  // how far the connector body extends inward from the +X wall

outer_x = esp_short_axis + 2*wall + usb_body_len + 6;
outer_y = esp_short_axis + 2*wall + 6;
outer_z = 12;   // box height (bumped to keep interior depth after thicker floor)
floor_thickness = 4;  // thicker than wall so the USB recess doesn't punch through
lid_gap = 0.2;  // clearance between box and lid (per side)

// USB cutout (on the +X wall, centered on Y)
usb_w = 12;     // aperture width
usb_h = 2;      // aperture height
usb_z = floor_thickness;  // slot bottom sits flush with inner floor surface

// USB connector body recess in the floor (pocket the connector body drops into)
usb_body_recess = 2;   // pocket depth (must be < floor_thickness or it punches through)

// Battery floor recess (opposite side from USB, below the esp holder)
battery_x = 30;        // length along X
battery_y = 20;        // width along Y
battery_recess = 2;    // pocket depth (must be < floor_thickness)

// Visibility toggles — flip to false to hide a component in preview
show_box         = true;
show_esp_holder  = true;
show_lid         = true;
show_lid_separately = true;  // when true, lid is offset for preview

// --- Box (open-top) ---
module box() {
    difference() {
        cube([outer_x, outer_y, outer_z]);
        // hollow interior, floor is `floor_thickness` thick
        translate([wall, wall, floor_thickness])
            cube([outer_x - 2*wall,
                  outer_y - 2*wall,
                  outer_z]); // cut all the way through the top

        // USB cutout through the +X wall, open all the way to the top of the box
        // (top of the aperture is closed by a tab on the lid)
        translate([outer_x - wall - 0.01,
                   (outer_y - usb_w) / 2,
                   usb_z])
            cube([wall + 0.02, usb_w, outer_z - usb_z + 0.01]);

        // Floor recess (pocket) for the USB connector body
        // Goes from the inner floor surface down by usb_body_recess
        translate([outer_x - usb_body_len,
                   (outer_y - usb_w) / 2,
                   floor_thickness - usb_body_recess])
            cube([usb_body_len + 0.02, usb_w, usb_body_recess + 0.01]);

        // Battery floor recess on the -X side, centered on Y
        translate([wall - 0.01,
                   (outer_y - battery_y) / 2,
                   floor_thickness - battery_recess])
            cube([battery_x + 0.01, battery_y, battery_recess + 0.01]);
    }
}

// --- Lid (sits on top, with a lip that drops into the box) ---
module lid() {
    lip_h = 2;
    lip_x = outer_x - 2*wall - 2*lid_gap;
    lip_y = outer_y - 2*wall - 2*lid_gap;

    // height of the USB tab — fills the open part of the box's USB slot,
    // so the tab's underside becomes the top of the USB aperture
    usb_tab_h = outer_z - (usb_z + usb_h);

    // flat top plate
    //%color([.8,.8,.2,.2])
    cube([outer_x, outer_y, wall]);
    // lip underneath that nests inside the box walls
    //%color([.8,.8,.2,.2])
    translate([wall + lid_gap, wall + lid_gap, -lip_h])
        cube([lip_x, lip_y, lip_h]);
    // USB tab — drops into the open-top slot in the +X wall
    translate([outer_x - wall,
               (outer_y - usb_w) / 2 + lid_gap,
               -usb_tab_h])
        cube([wall, usb_w - 2*lid_gap, usb_tab_h]);
}

// -- Build a smaller holder for the ESP, with some room for connections below
module esp_holder() {
    pcb_raise       = 5;   // how far above the floor the PCB sits (= shelf height)
    support_l       = 5;
    tower_above_pcb = 2;   // tower extends this far above the shelf, alongside the PCB

    // PCB (representation), raised by the support
    %color([.0,.6,.8,.3])
    %translate([wall+2, wall+3, floor_thickness + pcb_raise])
        cube([esp_long_axis, esp_short_axis, 5]);

    // Support wedge — knife edge at the floor (leaves the battery recess
    // clear), widens up to the PCB. Slope is 45° from the wall, which is
    // the steepest overhang most printers handle without supports.
    sup_z_bot = floor_thickness - battery_recess;
    sup_z_top = floor_thickness + pcb_raise;
    support_w = sup_z_top - sup_z_bot;  // 45° overhang cap

    translate([0,
               wall + 3 + esp_short_axis/2 + support_l/2,
               0])
    rotate([90, 0, 0])
    linear_extrude(support_l)
        polygon([
            [wall,              sup_z_bot],
            [wall,              sup_z_top],
            [wall + support_w,  sup_z_top]
        ]);

    // Side retainers on the long edges of the PCB (Y sides).
    // Each is an L sitting on the floor: wide base on the floor strip
    // outside the battery recess, 45° ramp up to a shelf at PCB level
    // (PCB drops onto the shelf — vertical support), then a tower
    // alongside the PCB above the shelf (lateral support).
    z_bot       = floor_thickness;
    z_shelf     = z_bot + pcb_raise;                // PCB rests on top of the shelf
    z_top       = z_shelf + tower_above_pcb;        // top of the tower
    base_y_in   = (outer_y - battery_y) / 2;        // battery recess edge (where the base ends)
    slope_y_top = base_y_in + pcb_raise;            // 45° ramp rises pcb_raise in y too
    pcb_y_edge  = wall + 3;                          // tower's inner edge (PCB's -Y edge)
    retainer_x   = wall + 2 + esp_long_axis/2;
    retainer_len = esp_long_axis/2;

    // -Y retainer
    translate([retainer_x, 0, 0])
    rotate([90, 0, 90])
    linear_extrude(retainer_len)
        polygon([
            [wall,        z_bot],         // base outer (at wall)
            [base_y_in,   z_bot],         // base inner (battery recess edge)
            [slope_y_top, z_shelf],       // top of 45° ramp = outer edge of shelf
            [pcb_y_edge,  z_shelf],       // shelf inner = tower outer (PCB rests here)
            [pcb_y_edge,  z_top],         // top of tower (PCB side)
            [wall,        z_top]          // top of tower (wall side)
        ]);

    // +Y retainer (mirror)
    translate([retainer_x, 0, 0])
    rotate([90, 0, 90])
    linear_extrude(retainer_len)
        polygon([
            [outer_y - wall,        z_bot],
            [outer_y - base_y_in,   z_bot],
            [outer_y - slope_y_top, z_shelf],
            [outer_y - pcb_y_edge,  z_shelf],
            [outer_y - pcb_y_edge,  z_top],
            [outer_y - wall,        z_top]
        ]);
}

// --- Render ---
if (show_box)        box();
if (show_esp_holder) esp_holder();

if (show_lid) {
    if (show_lid_separately)
        translate([0, outer_y + 5, 0]) lid();
    else
        translate([0, 0, outer_z]) lid();
}
