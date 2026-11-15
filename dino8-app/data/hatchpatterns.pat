;; Dino 8 hatch pattern library (AutoCAD .pat syntax).
;;
;;   *NAME, description
;;   angle, x-origin, y-origin, delta-x, delta-y [, dash, gap, dash, ...]
;;
;; Every line after a *NAME header is one family of parallel lines: `angle`
;; in degrees, the family passes through (x-origin, y-origin), successive
;; lines are offset by delta-y perpendicular to the family and shifted by
;; delta-x along it. Dash lengths are positive, gaps negative, 0 is a dot.
;; All lengths are in pattern units and are multiplied by the hatch scale.
;;
;; Drop your own .pat files next to this one (data/*.pat) or append to it:
;; the Hatch command's Pattern= option lists every pattern found.

*SOLID, Solid fill
45, 0,0, 0,1

*HATCH1, Single family of lines (legacy Dino 8 name)
0, 0,0, 0,1
*GRID, Square grid (legacy Dino 8 name)
0, 0,0, 0,1
90, 0,0, 0,1
*HATCH2, Lines with a sparser perpendicular family (legacy Dino 8 name)
0, 0,0, 0,1
90, 0,0, 0,2

*ANSI31, ANSI Iron, Brick, Stone masonry
45, 0,0, 0,.125
*ANSI32, ANSI Steel
45, 0,0, 0,.375
45, .176776695,0, 0,.375
*ANSI33, ANSI Bronze, Brass, Copper
45, 0,0, 0,.25
45, .176776695,0, 0,.25, .125,-.0625
*ANSI34, ANSI Plastic, Rubber
45, 0,0, 0,.75
45, .176776695,0, 0,.75
45, .353553391,0, 0,.75
45, .530330086,0, 0,.75
*ANSI35, ANSI Fire brick, Refractory material
45, 0,0, 0,.25
45, .176776695,0, 0,.25, .3125,-.0625,0,-.0625
*ANSI36, ANSI Marble, Slate, Glass
45, 0,0, .21875,.125, .3125,-.0625,0,-.0625
*ANSI37, ANSI Lead, Zinc, Magnesium, Sound/Heat/Elec Insulation
45, 0,0, 0,.125
135, 0,0, 0,.125
*ANSI38, ANSI Aluminum
45, 0,0, 0,.125
135, 0,0, .25,.125, .3125,-.1875

*ISO02W100, ISO dashed
0, 0,0, 0,5, 12,-3
*ISO03W100, ISO dashed space
0, 0,0, 0,5, 12,-18
*ISO04W100, ISO long dash dot
0, 0,0, 0,5, 24,-3,.5,-3
*ISO05W100, ISO long dash double dot
0, 0,0, 0,5, 24,-3,.5,-3,.5,-3
*ISO06W100, ISO long dash triple dot
0, 0,0, 0,5, 24,-3,.5,-3,.5,-3,.5,-3
*ISO07W100, ISO dot
0, 0,0, 0,5, .5,-3
*ISO08W100, ISO long dash short dash
0, 0,0, 0,5, 24,-3,6,-3
*ISO09W100, ISO long dash double short dash
0, 0,0, 0,5, 24,-3,6,-3,6,-3
*ISO10W100, ISO dash dot
0, 0,0, 0,5, 12,-3,.5,-3
*ISO11W100, ISO double dash dot
0, 0,0, 0,5, 12,-3,12,-3,.5,-3
*ISO12W100, ISO dash double dot
0, 0,0, 0,5, 12,-3,.5,-3,.5,-3
*ISO13W100, ISO double dash double dot
0, 0,0, 0,5, 12,-3,12,-3,.5,-3,.5,-3
*ISO14W100, ISO dash triple dot
0, 0,0, 0,5, 12,-3,.5,-3,.5,-3,.5,-3
*ISO15W100, ISO double dash triple dot
0, 0,0, 0,5, 12,-3,12,-3,.5,-3,.5,-3,.5,-3

*BRICK, Brick or masonry-type surface
0, 0,0, 0,.25
90, 0,0, 0,.5, .25,-.25
90, .25,0, 0,.5, -.25,.25
*HONEYCOMB, Honeycomb pattern
0, 0,0, .1875,.108253175, .125,-.25
120, 0,0, .1875,.108253175, .125,-.25
60, 0,0, .1875,.108253175, -.25,.125
*DOTS, A series of dots
0, 0,0, .03125,.0625, 0,-.0625
*CROSS, A series of crosses
0, 0,0, .25,.25, .125,-.375
90, .0625,-.0625, .25,.25, .125,-.375
*GRASS, Grass area
90, 0,0, .707106781,.707106781, .1875,-1.226713563
45, 0,0, 0,1, .1875,-.8125
135, 0,0, 0,1, .1875,-.8125
*GRAVEL, Cobble / gravel pattern
228.0127875, 0,.75, 0,1, .1,-.9
184.3987054, .25,.75, 0,1, .2,-.8
120, 0,.5, 0,1, .12,-.88
60, .5,.5, 0,1, .15,-.85
0, .25,.25, 0,1, .3,-.7
315, .75,.35, 0,1, .18,-.82
*NET, Horizontal / vertical grid
0, 0,0, 0,.125
90, 0,0, 0,.125
