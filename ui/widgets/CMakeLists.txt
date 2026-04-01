idf_component_register(
    SRCS
        "anim_registry.c"
        "animations_aurora.c"
        "animations_compass.c"
        "animations_helix.c"
        "animations_lattice.c"
        "animations_lissajous.c"
        "animations_nucleus.c"
        "animations_radar.c"
        "animations_vortex.c"
        "orb_widget.c"
    INCLUDE_DIRS "include"
    REQUIRES orb presentation lvgl
)
