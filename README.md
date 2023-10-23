# About
This repository is a fork of Lee "eihrul" Salzman's ragdoll editor. It is designed to generate ragdoll scripts for IQM and MD5 skeletal model formats supported by the Cube engines.  
Given the abstruse nature of the editor, the primary objective of this fork is to reorganize the files, tweak the code and provide satisfying documentation for using the editor, demystifying its complexity (as it's not as daunting as it may initially appear).

# License
The source code of this ragdoll editor is licensed under the [permissive zlib license](./LICENSE.md).
The editor's default setup includes an example scene: the `example/mrfixit.md5mesh` model (preconfigured in a basic way in the editor) is subject to a different license compared to the one chosen for the source code.
For additional information regarding this matter, please refer to the `example/mrfixit_readme` file.

# Basic commands
| Key                  | Action                 |
| ---------------------|------------------------|
| WASD/mouse           | move/strafe/mouse-look
| Q                    | up
| E                    | down
| BACKQUOTE            | command line
| F1                   | pause/unpause ragdoll simulation
| F2                   | step ragdoll simulation
| F5                   | toggle sphere visibility
| F6                   | toggle triangle visibility
| F7                   | toggle joint visibility
| F8                   | toggle model visibility
| C                    | toggle free-fly mode
| left mouse button    | click to select a sphere or triangle
| middle mouse button  | click/hold to drag a sphere
| right mouse button   | cancel selctions
| scroll wheel         | push selection cursor forward/back
| SPACE                | spawn a sphere
| BACKSPACE            | clear scene
| DELETE               | delete constraint between two selected items
| T                    | tie 2 or more selected spheres together with a distance constraint
| Y                    | make a triangle out of 3 spheres together
| N                    | toggle immovability on selected spheres
| F3                   | invert immovability on all spheres
| 1-9                  | tie 2 triangles together by a rotational constraint of 10-90 degrees
| -                    | quicksave scene
| =                    | quickload scene
