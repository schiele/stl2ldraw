# stl2ldraw

This is a utility to convert stl files to LDraw dat files.

This is still a prototype implementation with incomplete error
handling and other technical debt asking for further refactoring.
But in the end this was a weekend hack so far that actually works.

## Features

- Reads binary and ascii STL files.
- Merges very close vertexes that would not produce visual
  differences but might cause non-manifold structures.
- Warns about incorrect normal vectors in the STL files and corrects
  them.
- Warns about detected non-manifold structures but does not correct
  them:
  - degenerated faces (faces spanning no area, meaning all points on
    one line)
  - open edges
  - ambiguous edges
- Merges adjacent triangles on the same plane to convex
  quadrilaterals.
- Generates lines for sharp edges (more than 25 degrees angle) and
  open edges.
- Generates optional lines for soft convex edges (less than 25 degrees
  angle). For concave edges they are not generates since they would
  never be visible by their concave nature anyway.

