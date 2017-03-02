
$fn=50;

module inner() {
  union() {
    cube ([90,30,25], center=true);
    
    translate([6+26/2,0,25/2])
    hull() {
      cube ([27,23,1], center=true);
      translate([0,0,8]) cube ([27+15,23+8,1], center=true);
    }
    
    translate([-50,0,7]) rotate([0,90,0]) cylinder (d=8, h=100, center=true);
  }
}

module box() {
  difference() {
    translate([0,0,3]) cube ([90+2.5,30+2.5,25+5], center=true);
    inner();
    translate([-22.5,0,14]) cube ([45,30,5], center=true);
  }
}

module cap() {
  translate([0,0,-11.5])
  difference() {
    cube ([89.5,29.5,1], center=true);
    cylinder (d=20, h=2, center=true);
  }
}

translate([0,30,0]) box();
translate([0,-10,0]) cap();
