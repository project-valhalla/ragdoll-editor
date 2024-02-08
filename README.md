# About
This repository is a fork of Lee "eihrul" Salzman's ragdoll editor. It is designed to generate ragdoll scripts for IQM and MD5 skeletal model formats supported by the Cube engines.  
Given the abstruse nature of the editor, the primary objective of this fork is to reorganize the files, tweak the code and provide satisfying documentation for using the editor, demystifying its complexity (as it's not as daunting as it may initially appear).

# License
The source code of this ragdoll editor is licensed under the [permissive zlib license](./LICENSE.md).
The editor's default setup includes an example scene: the `example/mrfixit.md5mesh` model (preconfigured in a basic way in the editor) is subject to a different license compared to the one chosen for the source code.
For additional information regarding this matter, please refer to the model's [readme file](./example/mrfixit_readme.txt).

# Basic commands
| Key                  | Action                 |
| ---------------------|------------------------|
| -                    | quicksave scene
| 1-9                  | tie 2 triangles together by a rotational constraint of 10-90 degrees
| =                    | quickload scene
| BACKQUOTE            | command line
| BACKSPACE            | clear scene
| C                    | toggle free-look mode
| DELETE               | delete constraint between two selected items
| E                    | up
| F1                   | pause/unpause ragdoll simulation
| F2                   | step ragdoll simulation
| F3                   | invert immovability on all spheres
| F5                   | toggle sphere visibility
| F6                   | toggle triangle visibility
| F7                   | toggle joint visibility
| F8                   | toggle model visibility
| Left mouse button    | click to select a sphere or triangle
| Left shift           | sprint (hold to increase fly speed)
| Middle mouse button  | click/hold to drag a sphere
| Mouse                | mouse-look (requires free-look to be enabled)
| N                    | toggle immovability on selected spheres
| Q                    | down
| Right mouse button   | cancel selections
| Scroll wheel         | push selection cursor forward/back
| SPACE                | spawn a sphere
| T                    | tie 2 or more selected spheres together with a distance constraint
| WASD/arrow keys      | move/strafe
| Y                    | make a triangle out of 3 spheres together
