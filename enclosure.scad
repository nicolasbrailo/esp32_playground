// Basic parametric box enclosure
// Units are millimeters (OpenSCAD convention)

// --- Parameters ---
esp_long_axis = 23;
esp_short_axis = 18;
wall    = 1.5;    // wall thickness
usb_body_len    = 21;  // how far the connector body extends inward from the +X wall

outer_x = esp_short_axis + 2*wall + usb_body_len + 15;
outer_y = esp_short_axis + 2*wall + 6;
outer_z = 20;   // box height
lid_gap = 0.1;  // clearance between box and lid (per side)

/* Floor can bow
  Fixes, roughly in order of impact:                                           
  1. Thicken the floor — in your slicer set bottom layers to 5–6 (or in CAD make it ≥1.2 mm). Single biggest win for thin floors.                                                                                                               
  2. Add a brim (5–8 mm) to hold edges down while it cools.                                                                                         
  3. Heated bed at the right temp — PLA 55–65 °C, PETG 70–80 °C, ABS 100–110 °C + enclosure.                                                                                     
  4. Slow the first layer (15–20 mm/s) and disable the part-cooling fan for the first 3–5 layers.  
*/
floor_thickness = 3;  // thicker than wall so the USB recess doesn't punch through


// USB cutout (on the +X wall, centered on Y)
usb_w = 12;     // aperture width
usb_h = 2;      // aperture height
usb_z = floor_thickness;  // slot bottom sits flush with inner floor surface

// USB connector body recess in the floor (pocket the connector body drops into)
usb_body_recess = 2;   // pocket depth (must be < floor_thickness or it punches through)

// Battery floor recess (opposite side from USB, below the esp holder)
battery_x = 30;        // length along X
battery_y = 22;        // width along Y
battery_recess = 2;    // pocket depth (must be < floor_thickness)


esp_pcb_raise       = 7;   // how far above the floor the PCB sits (= shelf height)

// Lid lip — perimeter frame nests inside the box walls; middle is hollow
// to save material and add interior headroom.
lip_h       = 1.5;
lip_frame_w = 1;   // width of the perimeter frame

// Button plunger — captive in the lid, presses the PCB's on-board button.
// Plunger is printed separately and drops into the lid hole from inside
// before the lid is assembled. The flange is wider than the hole, so it
// stays captive even when the box is held upside down.
button_x              = wall + 2 + 10;  // 10mm in X from the PCB origin
button_y              = wall + 3 + 4;   //  4mm in Y from the PCB origin
button_top_z          = 12;             // height of the real button's top above the box floor
plunger_shaft_d       = 3;
plunger_flange_d      = 6;
plunger_flange_h      = 1;
plunger_top_above_lid = 8;              // shaft pokes this far above the lid for finger-press
plunger_hole_d        = plunger_shaft_d + 0.4;  // shaft + clearance
plunger_pad_d         = plunger_flange_d + .0;   // lip pad around the hole that keeps the flange seated

// Center hole — through the lid in the -Y half (opposite the gills).
lid_hole_d = 12.5;
lid_hole_x = 3 * outer_x / 4;
lid_hole_y = outer_y / 2;

// Gills — vertical slots in the +Y half of the lid (opposite the plunger),
// cut through the lid plate and the lip so the on-board LED shines through.
gill_count = 7;
gill_w     = 2;                                                // slot width along X
gill_pitch = 3.5;                                                // X spacing between slot centers
gill_y_lo  = outer_y / 2 + 1 ;                                  // start past the lid Y midline
gill_y_hi  = outer_y - wall - 1;                               // end just inside the +Y wall
gill_x_lo  = (outer_x - ((gill_count - 1) * gill_pitch + gill_w)) / 2 - 10;

// Visibility toggles — flip to false to hide a component in preview
show_box         = true;
show_esp_holder  = true;
show_lid         = true;
show_assembled = false;  // when true, lid is offset for preview
show_pcbs = false;

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
                   (outer_y - usb_w) / 2 - 0.02,
                   floor_thickness - usb_body_recess])
            cube([usb_body_len + 0.02, usb_w + 0.02, usb_body_recess + 0.01]);

        // Battery floor recess on the -X side, centered on Y
        translate([wall - 0.01,
                   (outer_y - battery_y) / 2,
                   floor_thickness - battery_recess])
            cube([battery_x + 0.01, battery_y, battery_recess + 0.01]);
    }
    
    // Holder for back of usb pcb
    translate([36, 4.2, 7.5])
    rotate([0, 90, 0])
    linear_extrude(5.5)
        polygon([
            [3, 4.5],
            [5, 3],
            [3, 3],
        ]);
    translate([41.5, 22.8, 7.5])
    rotate([180, 90, 0])
    linear_extrude(5.5)
        polygon([
            [3, 4.5],
            [5, 3],
            [3, 3],
        ]);
}

// --- Lid (sits on top, with a lip that drops into the box) ---
module lid() {
    lip_x = outer_x - 2*wall - 2*lid_gap;
    lip_y = outer_y - 2*wall - 2*lid_gap;

    // height of the USB tab — fills the open part of the box's USB slot,
    // so the tab's underside becomes the top of the USB aperture
    usb_tab_h = outer_z - (usb_z + usb_h);

    difference() {
        union() {
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
        // Plunger hole (through the lid plate and the lip below it)
        translate([button_x, button_y, -lip_h - 0.01])
            cylinder(d=plunger_hole_d, h=wall + lip_h + 0.02, $fn=32);

        // Center hole through the -Y half of the lid (opposite the gills)
        translate([lid_hole_x, lid_hole_y, -lip_h - 0.01])
            cylinder(d=lid_hole_d, h=wall + lip_h + 0.02, $fn=64);

        // Gills — let the on-board LED shine through the +Y half of the lid
        for (i = [0 : gill_count - 1])
            translate([gill_x_lo + i * gill_pitch, gill_y_lo, -lip_h - 0.01])
                cube([gill_w, gill_y_hi - gill_y_lo, wall + lip_h + 0.02]);

        // Hollow out the middle of the lip — keep a perimeter frame plus
        // a pad around the plunger hole so the flange still seats on lip.
        difference() {
            translate([wall + lid_gap + lip_frame_w,
                       wall + lid_gap + lip_frame_w,
                       -lip_h - 0.01])
                cube([lip_x - 2*lip_frame_w,
                      lip_y - 2*lip_frame_w,
                      lip_h + 0.02]);
            translate([button_x, button_y, -lip_h - 0.02])
                cylinder(d=plunger_pad_d, h=lip_h + 0.04, $fn=32);
        }
    }
}

// --- Plunger (separate part, captive in the lid hole) ---
module plunger() {
    $fn = 32;
    // Flange sits inside the box, under the lid lip — wider than the hole
    // so the plunger can't fall out when the device is inverted.
    cylinder(d=plunger_flange_d, h=plunger_flange_h);
    // Shaft runs from the flange up through the lip, the plate, and
    // pokes out the top by `plunger_top_above_lid` for finger-pressing.
    cylinder(d=plunger_shaft_d,
             h=plunger_flange_h + lip_h + wall + plunger_top_above_lid);
}

// -- Build a smaller holder for the ESP, with some room for connections below
module esp_holder() {
    support_l       = 6;
    tower_above_pcb = 2;   // tower extends this far above the shelf, alongside the PCB

    // Support wedge — knife edge at the floor (leaves the battery recess
    // clear), widens up to the PCB. Slope is 45° from the wall, which is
    // the steepest overhang most printers handle without supports.
    sup_z_bot = floor_thickness - battery_recess;
    sup_z_top = floor_thickness + esp_pcb_raise;
    support_w = sup_z_top - sup_z_bot;  // 45° overhang cap

    // -.01 to force wedge to merge with the box
    translate([0 - .01,
               wall + 3 + esp_short_axis/2 + support_l/2 -.01,
               0 - .01])
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
    z_shelf     = z_bot + esp_pcb_raise;                // PCB rests on top of the shelf
    z_top       = z_shelf + tower_above_pcb;        // top of the tower
    base_y_in   = (outer_y - battery_y) / 2;        // battery recess edge (where the base ends)
    stopper_y_adj = 3;  // This adjusts how much the stopper and retainer project from the walls. A bigger adjustment means less projection and gentler slope => battery has more space
    slope_y_top = base_y_in + esp_pcb_raise-stopper_y_adj;            // 45° ramp rises esp_pcb_raise in y too
    pcb_y_edge  = wall + 3;                          // tower's inner edge (PCB's -Y edge)
    retainer_x   = wall + 7 + esp_long_axis/2;
    retainer_len = esp_long_axis/3;

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

    // X-stopper at the +X end of each retainer: replace the L's last
    // `stopper_len` with a solid wedge (45° ramp continues all the way
    // to z_top, no shelf notch). The wedge intrudes into the PCB area
    // above the shelf, so the PCB butts up against it and can't slide
    // along X.
    stopper_len  = 1;
    stopper_apex = base_y_in + (z_top - z_bot)-stopper_y_adj;   // 45° ramp's y at z_top

    // -Y stopper
    translate([retainer_x + retainer_len - stopper_len, 0, 0])
    rotate([90, 0, 90])
    linear_extrude(stopper_len)
        polygon([
            [wall,         z_bot],
            [base_y_in,    z_bot],
            [stopper_apex, z_top],
            [wall,         z_top]
        ]);

    // +Y stopper (mirror)
    translate([retainer_x + retainer_len - stopper_len, 0, 0])
    rotate([90, 0, 90])
    linear_extrude(stopper_len)
        polygon([
            [outer_y - wall,         z_bot],
            [outer_y - base_y_in,    z_bot],
            [outer_y - stopper_apex, z_top],
            [outer_y - wall,         z_top]
        ]);
}

module pcbs() {
    // ESP PCB (representation), raised by the support
    %color([.0,.6,.8,.3])
    %translate([wall+2, wall+3, floor_thickness + esp_pcb_raise])
        cube([esp_long_axis, esp_short_axis, 5]);

    // Battery (representation) — 19×26×4mm, rotated so the 26mm side
    // runs along X to fit the 30×20 floor recess. Bottom sits in the
    // recess; the 4mm battery sticks up 2mm past the floor surface.
    %color([.7,.5,.1,.3])
    %translate([wall + 2, (outer_y - 19)/2, floor_thickness - battery_recess])
        cube([26, 19, 4]);

    // USB PCB (representation) — 30×12×3mm. Sits in the floor recess
    // channel that cuts through the +X wall; the +X end protrudes 5mm
    // past the box for the connector to plug into.
    %color([.2,.2,.8,.3])
    %translate([outer_x - 16, (outer_y - usb_w)/2, floor_thickness - usb_body_recess])
        cube([30, usb_w, 3]);
}

// --- Render ---

if (show_box) {
    union() {
        box();
        esp_holder();
    }
}

if (show_lid) {
    if (show_assembled) {
        translate([0, 0, outer_z]) lid(); 
        translate([button_x, button_y, button_top_z]) plunger();
    } else {
        translate([0, 2*outer_y + 5, 2]) rotate([180, 0, 0]) lid();
        translate([5, 3*outer_y - 10, 0]) plunger();
    }
}

if (show_pcbs) {
    pcbs();
}
