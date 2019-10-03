/*
 * Copyright (c) 2017, Stephen Warren
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

$fn = 30;

pcb_w = 45 + 1.5;
pcb_h = 20 + 2;
wall_thick = 3;
horn_r = 15.75 / 2;
horn_r_slop = 0.75;
horn_space = 41.5 - (2 * horn_r);
supp_h = 3.5;
supp_l = 7;
mount_depth = wall_thick + supp_h + 9;
flange_l = 10 + wall_thick;
clip_thick = 2;
clip_interior = 9.5;
clip_wide = 9;
clip_depth = 14;

module corner_dist(w, h) {
    translate([-w / 2, -h / 2, 0])
        rotate(0) children();
    translate([-w / 2,  h / 2, 0])
        rotate(270) children();
    translate([ w / 2, -h / 2, 0])
        rotate(90) children();
    translate([ w / 2,  h / 2, 0])
        rotate(180) children();
}

module hc_sr04_mount() {
    // Front face plate, with cut-outs for horns
    base_w = pcb_w + (2 * wall_thick);
    base_h = pcb_h;
    difference() {
        cube([base_w, base_h, wall_thick], center=true);
        translate([-horn_space / 2, 0, 0])
            cylinder(r=horn_r + horn_r_slop, h=(2 * wall_thick), center=true);
        translate([ horn_space / 2, 0, 0])
            cylinder(r=horn_r + horn_r_slop, h=(2 * wall_thick), center=true);
    }

    // Side walls
    wall_x_xlat = (base_w - wall_thick) / 2;
    translate([-wall_x_xlat, 0, (mount_depth - wall_thick) / 2])
        cube([wall_thick, pcb_h, mount_depth], center=true);
    translate([ wall_x_xlat, 0, (mount_depth - wall_thick) / 2])
        cube([wall_thick, pcb_h, mount_depth], center=true);

    // Corner supports
    corner_dist(w=pcb_w, h=pcb_h) {
        translate([0, 0, wall_thick / 2])
            linear_extrude(height=supp_h)
            polygon([[0, 0], [supp_l, 0], [0, supp_l]]);
    }
}

module hc_sr04_flange_a() {
    translate([0, 0, (mount_depth - wall_thick) / 2])
        cube([wall_thick, pcb_h, mount_depth], center=true);
    translate([(flange_l - wall_thick) / 2, 0, 0])
        cube([flange_l, pcb_h, wall_thick], center=true);
}

module hc_sr04_flange_b() {
    translate([0, 0, (mount_depth - wall_thick) / 2])
        cube([wall_thick, pcb_h, mount_depth], center=true);
    translate([(flange_l - wall_thick) / 2, (pcb_h - wall_thick) / 2, (mount_depth - wall_thick)/ 2])
        cube([flange_l, wall_thick, mount_depth], center=true);
}

module hc_sr04_flange_clip() {
    cube([clip_thick, clip_depth + clip_thick, clip_wide], center=true);
    translate([clip_interior + clip_thick, 0, 0])
        cube([clip_thick, clip_depth + clip_thick, clip_wide], center=true);
    translate([(clip_interior + clip_thick) / 2, clip_depth / 2, 0])
        cube([clip_interior + (2 * clip_thick), clip_thick, clip_wide], center=true);
}

hc_sr04_mount();

translate([ ((pcb_w / 2) + (4 * wall_thick)), 0, 0]) hc_sr04_flange_a();
//translate([-((pcb_w / 2) + (4 * wall_thick)), 0, 0]) rotate(180) hc_sr04_flange_a();

//translate([ ((pcb_w / 2) + (4 * wall_thick)), 0, 0]) hc_sr04_flange_b();
translate([-((pcb_w / 2) + (4 * wall_thick)), 0, 0]) rotate(180) hc_sr04_flange_b();

translate([0, -25, 0]) hc_sr04_flange_clip();
