# Outils (Tools)

- `banc_test_simavr/` : virtual test bench that runs compiled Arduino Uno firmware in simavr with simulated sensors, LCD, servo, LEDs and buzzer.
- `labo3d/` : 3D lab. `labo3d.py model.scad part1 part2` builds STL files with OpenSCAD, checks the mesh (watertight, one piece, dimensions with trimesh) and renders four views to a PNG.
  Needs: `sudo apt install openscad xvfb`, `pip install trimesh manifold3d pillow numpy`.
  Example: `python3 labo3d.py boitier_parking.scad base couvercle`

Limits: simplified models, never a replacement for a real print or a test on real hardware.
