# Plugins
- [Fabrication Toolkit](https://github.com/bennymeg/Fabrication-Toolkit)
- [Replicate Layout](https://github.com/MitjaNemec/ReplicateLayout)

# Template Setup
## Preferences -> Configure Paths...
- KICAD_USER_TEMPLATE_DIR=~/workspace/circuits/templates

# Component Setup
## easyeda2kicad
 - [github](https://github.com/uPesy/easyeda2kicad.py)
 - `easyeda2kicad --output ~/workspace/circuits/libraries/easyeda2kicad --full --lcsc_id=...`
## Schematics Editor -> Preferences -> Manage Symbol Libraries...
easyeda2kicad -> circuits/libraries/easyeda2kicad/easyeda2kicad.kicad_sym
## PCB Editor -> Preferences -> Manage Footprint Libraries...
easyeda2kicad -> circuits/libraries/easyeda2kicad/easyeda2kicad.pretty
