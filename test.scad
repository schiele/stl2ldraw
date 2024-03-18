$fn=16;

render() scale(2/5) {
    difference() {
        translate([0, 0, -12]) cube([80, 40, 24], center=true);
        translate([0, 0, -16]) cube([72, 32, 24], center=true);
    }
    for(x=[-30:20:30], y=[-10, 10]) translate([x, y, 0])
        cylinder(8, d=12, center=true);
    for(x=[-20:20:20]) translate([x, 0, -24]) linear_extrude(24)
        difference() {
            circle(d=16);
            circle(d=12);
        }
}
