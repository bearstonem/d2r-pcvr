// The menu's icon set (original artwork drawn for this project;
// no third-party paths). 48 x 48 grid. Kinds: S solid (ink outline under it),
// H solid with even-odd holes, B band (stroke of width w over an ink stroke w+2),
// D carved detail line (ink 55%, 1.5 px), F carved area (ink 45%).
// Transform 1 / 2: rotate +45 / -45 degrees and scale 1.1 about the center.
static const IconLayer Icon_body[]={
    {'S',0,0,"M14 3.5h15.5l-1 17.5c0 2.2 1 3.7 3.2 4.6l8.6 3.7c2.8 1.3 4.2 3.4 4.2 6.2V39H8.5l-.3-2.4c-.4-4.4 1.6-9.4 3.3-13.3z"},
    {'S',0,0,"M8 41.5h36.5V45H8z"},
    {'D',0,0,"M14.2 9h15M28.8 25.5l-3.6 3M33.5 27.5l-3.6 3M17 41.5V45"},
};
static const IconLayer Icon_combat[]={
    {'S',0,1,"M24 1.5l4.2 5.2V28.5h-8.4V6.7z"},
    {'S',0,1,"M13 28.5h22v4.6H13z"},
    {'S',0,1,"M21.5 33.1h5v7.4h-5z"},
    {'S',0,1,"M20.7 43.6a3.3 3.3 0 1 0 6.6 0a3.3 3.3 0 1 0 -6.6 0z"},
    {'D',0,1,"M24 7.5v19"},
    {'S',0,2,"M24 1.5l4.2 5.2V28.5h-8.4V6.7z"},
    {'S',0,2,"M13 28.5h22v4.6H13z"},
    {'S',0,2,"M21.5 33.1h5v7.4h-5z"},
    {'S',0,2,"M20.7 43.6a3.3 3.3 0 1 0 6.6 0a3.3 3.3 0 1 0 -6.6 0z"},
    {'D',0,2,"M24 7.5v19"},
};
static const IconLayer Icon_aim[]={
    {'H',0,0,"M24 6a18 18 0 1 0 0 36 18 18 0 1 0 0-36zm0 5a13 13 0 1 1 0 26 13 13 0 1 1 0-26z"},
    {'S',0,0,"M22 1.5h4v13h-4zM22 33.5h4v13h-4zM1.5 22h13v4h-13zM33.5 22h13v4h-13z"},
    {'S',0,0,"M20.4 24a3.6 3.6 0 1 0 7.2 0a3.6 3.6 0 1 0 -7.2 0z"},
};
static const IconLayer Icon_targets[]={
    {'H',0,0,"M4.5 24C10 14.5 16.5 10.5 24 10.5S38 14.5 43.5 24C38 33.5 31.5 37.5 24 37.5S10 33.5 4.5 24zM24 15.5a8.5 8.5 0 1 0 0 17 8.5 8.5 0 1 0 0-17z"},
    {'S',0,0,"M19.4 24a4.6 4.6 0 1 0 9.2 0a4.6 4.6 0 1 0 -9.2 0z"},
    {'S',0,0,"M2 2h11v4H6v7H2zM46 2H35v4h7v7h4zM2 46h11v-4H6v-7H2zM46 46H35v-4h7v-7h4z"},
};
static const IconLayer Icon_shield[]={
    {'S',0,0,"M24 2.5l19 6.4V22c0 11.3-7.6 19.2-19 23.5C12.6 41.2 5 33.3 5 22V8.9z"},
    {'D',0,0,"M24 7.4l14.6 4.9V22c0 9-5.9 15.6-14.6 19.4C15.3 37.6 9.4 31 9.4 22v-9.7z"},
    {'F',0,0,"M24 17.5a5 5 0 1 0 0 10 5 5 0 1 0 0-10z"},
    {'D',0,0,"M24 7.6v9.6M24 27.8v13.4"},
};
static const IconLayer Icon_wrist[]={
    {'H',0,0,"M6.5 5Q24 .5 41.5 5L35.5 38Q24 42 12.5 38zM24 13l8.5 8.5-8.5 8.5-8.5-8.5z"},
    {'S',0,0,"M24 17.3l4.2 4.2-4.2 4.2-4.2-4.2z"},
    {'S',0,0,"M12 40.5Q24 44.5 36 40.5l.8 3.6Q24 48 11.2 44.1z"},
    {'F',0,0,"M7.2 8.8Q24 4.5 40.8 8.8l-.6 3.6Q24 8 7.8 12.4zM12 31.5Q24 35.5 36 31.5l-.5 3Q24 38.5 12.5 34.5z"},
    {'D',0,0,"M11.5 17l3 2.5-3 2.5M11.5 22l3 2.5-3 2.5M36.5 17l-3 2.5 3 2.5M36.5 22l-3 2.5 3 2.5"},
};
static const IconLayer Icon_interface[]={
    {'S',0,0,"M11 9h26v30H11z"},
    {'S',0,0,"M8.5 3h31a3.75 3.75 0 0 1 0 7.5h-31a3.75 3.75 0 0 1 0-7.5z"},
    {'S',0,0,"M8.5 37.5h31a3.75 3.75 0 0 1 0 7.5h-31a3.75 3.75 0 0 1 0-7.5z"},
    {'D',0,0,"M16 16.5h16M16 22h16M16 27.5h16M16 33h10M9 6.75h30M9 41.25h30"},
};
static const IconLayer Icon_survival[]={
    {'S',0,0,"M17.5 2.5h13v5.5h-13z"},
    {'S',0,0,"M19.5 9.5h9v6.3c6.4 2 10.5 7.8 10.5 14.2a15 15 0 0 1-30 0c0-6.4 4.1-12.2 10.5-14.2z"},
    {'F',0,0,"M24 41c-5.4-3.6-8.6-6.6-8.6-10a4 4 0 0 1 8.6-1.8 4 4 0 0 1 8.6 1.8c0 3.4-3.2 6.4-8.6 10z"},
    {'D',0,0,"M12 26.5c4-1.6 8-1.6 12 0s8 1.6 12 0"},
};
static const IconLayer Icon_world[]={
    {'H',0,0,"M12 45V18H8.5V4.5H15v4.5h4V4.5h10V9h4V4.5h6.5V18H36v27zM19.5 45v-7.5a4.5 4.5 0 0 1 9 0V45zM22.5 22h3v8h-3z"},
    {'D',0,0,"M12.3 18h23.4M12 31h4M32 31h4M12 25h3M33 25h3"},
};
static const IconLayer Icon_haptics[]={
    {'S',0,0,"M16.5 11.2a2.6 2.6 0 0 1 5.2 0V29h-5.2z"},
    {'S',0,0,"M22.2 8.2a2.6 2.6 0 0 1 5.2 0V28h-5.2z"},
    {'S',0,0,"M27.9 10.2a2.6 2.6 0 0 1 5.2 0V28h-5.2z"},
    {'S',0,0,"M33.6 15a2.4 2.4 0 0 1 4.8 0v14h-4.8z"},
    {'S',0,0,"M16.5 24h21.9v9c0 6.6-4.7 11.5-11 11.5h-.4c-4.6 0-8.2-2-10.5-5.6l-7.4-11a2.9 2.9 0 0 1 4.6-3.5l2.8 3.1z"},
    {'D',0,0,"M17 38.5h20"},
    {'B',3,0,"M8.5 6.5c-2.8 3.3-2.8 8.4 0 11.8M3.8 3.2c-4.3 5.2-4.3 13.2 0 18.4"},
    {'B',3,0,"M42.5 8c2.4 3 2.4 7.6 0 10.6M45.2 4.8c3.4 4.4 3.4 11.6 0 16"},
};
static const IconLayer Icon_performance[]={
    {'S',0,0,"M8.5 2.5h31v5h-31zM8.5 40.5h31v5h-31z"},
    {'H',0,0,"M12.5 7.5h23c0 8.5-5.2 12-8.3 16.5 3.1 4.5 8.3 8 8.3 16.5h-23c0-8.5 5.2-12 8.3-16.5-3.1-4.5-8.3-8-8.3-16.5zM16.2 10.5h15.6c-.8 5.6-4.7 8.6-6.5 13.1v.8c1.8 4.5 5.7 7.5 6.5 13.1H16.2c.8-5.6 4.7-8.6 6.5-13.1v-.8c-1.8-4.5-5.7-7.5-6.5-13.1z"},
    {'S',0,0,"M18.5 14h11c-1 2.6-3 4.6-5.5 7.6-2.5-3-4.5-5-5.5-7.6zM17.6 37.6h12.8c-1.2-3.4-3.8-5.8-6.4-5.8s-5.2 2.4-6.4 5.8z"},
    {'D',0,0,"M24 22v9"},
};
static const IconLayer Icon_controls[]={
    {'H',0,0,"M15 12.5h18c5.6 0 8.8 3.5 9.9 8.8l2 9.8c.9 4.4-3.2 7.2-6.5 4.7l-5.9-4.9H15.5l-5.9 4.9c-3.3 2.5-7.4-.3-6.5-4.7l2-9.8C6.2 16 9.4 12.5 15 12.5zM13.3 18.5h3.4v4h4v3.4h-4v4h-3.4v-4h-4v-3.4h4zM32 17.6a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 1 0 0-4.8zM36.6 22.4a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 1 0 0-4.8zM27.4 22.4a2.4 2.4 0 1 0 0 4.8 2.4 2.4 0 1 0 0-4.8z"},
};
static const IconLayer Icon_notes[]={
    {'S',0,0,"M3 9.5c7.5-2.3 14.6-1.3 19.8 3V42C17.6 37.7 10.5 36.7 3 39z"},
    {'S',0,0,"M45 9.5c-7.5-2.3-14.6-1.3-19.8 3V42c5.2-4.3 12.3-5.3 19.8-3z"},
    {'S',0,0,"M33 9v15l3.2-2.6L39.4 24V8.6z"},
    {'D',0,0,"M7.5 16.5c3.8-.8 7.5-.4 11 1.2M7.5 22c3.8-.8 7.5-.4 11 1.2M7.5 27.5c3.8-.8 7.5-.4 11 1.2M7.5 33c3.8-.8 7.5-.4 11 1.2M29.5 27.5c3.8-1.6 7.3-2 11-1.2M29.5 33c3.8-1.6 7.3-2 11-1.2"},
};
static const IconLayer Icon_presets[]={
    {'S',0,0,"M24 2.5l19.5 10.8v21.4L24 45.5 4.5 34.7V13.3z"},
    {'F',0,0,"M24 24l19.5-10.7v21.4L24 45.5z"},
    {'D',0,0,"M4.8 13.4L24 24l19.2-10.6M24 24v21.2"},
    {'D',0,0,"M14.3 25.5v11M11 29.5l6.6 3.6M30.5 27.5l6.5 3.6M37 27.5l-6.5 3.6"},
    {'D',0,0,"M24 8.5l8 4.4-8 4.4-8-4.4z"},
};
static const IconLayer Icon_comfort[]={
    {'S',0,0,"M41.5 3.5C30 4.5 18.5 12.5 13.5 30l-2.4 4.6 4.6-2.2C32 29.5 40.5 18 41.5 3.5z"},
    {'B',3,0,"M13 35.5l-6.5 9"},
    {'D',0,0,"M13.6 33.6L38.5 7.5M21 25.5l-2.8-6M26 20.5l-1.2-7M30.6 15.8l.2-6M24.5 22l6.8 1.4M29.5 17l7 .6"},
};
static const IconLayer Icon_classic[]={
    {'H',0,0,"M8.5 20.5C8.5 11 15.5 4 24 4s15.5 7 15.5 16.5v19.8c0 2.2-1.6 3.7-3.7 3.7H12.2c-2.1 0-3.7-1.5-3.7-3.7zM12.5 22.5h23v4h-9.5v12h-4v-12h-9.5z"},
    {'D',0,0,"M24 4.6v14.2M9 18.5h30M15.5 32.5h1M15.5 36.5h1M31.5 32.5h1M31.5 36.5h1"},
};
static const IconLayer Icon_fullvr[]={
    {'S',0,0,"M1.5 20h5v9h-5zM41.5 20h5v9h-5z"},
    {'S',0,0,"M10.5 11.5h27c3.3 0 5.5 2.2 5.5 5.5v14.5c0 3.3-2.2 5.5-5.5 5.5h-8.7l-3.2-4.6h-3.2L19.2 37h-8.7C7.2 37 5 34.8 5 31.5V17c0-3.3 2.2-5.5 5.5-5.5z"},
    {'F',0,0,"M10 18.5h11a2 2 0 0 1 2 2v6.5a2 2 0 0 1-2 2H10a2 2 0 0 1-2-2v-6.5a2 2 0 0 1 2-2zM27 18.5h11a2 2 0 0 1 2 2v6.5a2 2 0 0 1-2 2H27a2 2 0 0 1-2-2v-6.5a2 2 0 0 1 2-2z"},
    {'D',0,0,"M12 15h24"},
};
static const IconLayer Icon_fast[]={
    {'S',0,0,"M26.5 2h12l-8 14.5H41L16 46.5l5.5-20.5H9.5z"},
    {'D',0,0,"M29 5.5l-6.5 13"},
};
static const IconLayer Icon_changed[]={
    {'S',0,0,"M24.5 2.5c1.6 7.6 12.5 12 12.5 24.5a13 13 0 0 1-26 0c0-6.4 2.8-10.6 5.8-13.8.2 4.4 1.8 7 4.5 8.4C20 15 21.4 8.5 24.5 2.5z"},
    {'F',0,0,"M24 25.5c4.6 3.6 6.8 6.9 6.8 10.2a6.8 6.8 0 0 1-13.6 0c0-3.3 2.4-6 6.8-10.2z"},
};
static const IconLayer Icon_default[]={
    {'S',0,0,"M14.5 3.5h19L24 15z"},
    {'S',0,0,"M21.5 17.5h5V33h-5z"},
    {'S',0,0,"M4 33h40v5.5H4z"},
    {'S',0,0,"M4 26.5h4v6.5H4zM40 26.5h4v6.5h-4z"},
};
static const IconLayer Icon_padlock[]={
    {'B',5,0,"M15.5 22v-6.5a8.5 8.5 0 0 1 17 0V22"},
    {'H',0,0,"M12 20.5h24a3 3 0 0 1 3 3V41a3 3 0 0 1-3 3H12a3 3 0 0 1-3-3V23.5a3 3 0 0 1 3-3zM24 26a3.6 3.6 0 0 0-1.9 6.7l-1 6.3h5.8l-1-6.3A3.6 3.6 0 0 0 24 26z"},
};
static const IconLayer Icon_feel[]={
    {'S',0,0,"M15 3.5h5V21a4 4 0 0 0 8 0V3.5h5V21c0 4.4-2.8 7.9-6.5 9V36h-5v-6C17.8 28.9 15 25.4 15 21z"},
    {'S',0,0,"M21.5 35h5v6.5a2.5 2.5 0 0 1-5 0z"},
    {'B',3,0,"M9.5 5c-2.5 3-2.5 7.6 0 10.6M38.5 5c2.5 3 2.5 7.6 0 10.6"},
    {'B',3,0,"M4.5 2.5c-3.6 4.7-3.6 11 0 15.6M43.5 2.5c3.6 4.7 3.6 11 0 15.6"},
};
static const IconLayer Icon_reset[]={
    {'B',5,0,"M9.5 25.5A14.5 14.5 0 1 0 14 14.6"},
    {'S',0,0,"M4.5 7.5v14.5H19z"},
};
static const IconLayer Icon_undo[]={
    {'B',5,0,"M15 17.5h15.5a11 11 0 0 1 0 22H18"},
    {'S',0,0,"M2.5 17.5L16.5 5v25z"},
};
static const IconLayer Icon_search[]={
    {'H',0,0,"M20 5a15 15 0 1 0 0 30 15 15 0 1 0 0-30zm0 5.2a9.8 9.8 0 1 1 0 19.6 9.8 9.8 0 1 1 0-19.6z"},
    {'S',0,0,"M28.6 32.4l3.8-3.8 12.3 12.3a2.7 2.7 0 0 1-3.8 3.8z"},
};
static const IconLayer Icon_close[]={
    {'S',0,0,"M9.5 4.5L24 19 38.5 4.5l5 5L29 24l14.5 14.5-5 5L24 29 9.5 43.5l-5-5L19 24 4.5 9.5z"},
};
static const IconLayer Icon_check[]={
    {'S',0,0,"M3.5 25.5l5.6-5.6 9.4 9.4L38.9 8.9l5.6 5.6-26 26z"},
};
static const IconLayer Icon_info[]={
    {'H',0,0,"M24 3a21 21 0 1 0 0 42 21 21 0 1 0 0-42zM24 10.5a3 3 0 1 0 0 6 3 3 0 1 0 0-6zM21.2 20h5.6v17h-5.6z"},
};
static const IconLayer Icon_warning[]={
    {'H',0,0,"M24 3l22 39.5H2zM21.6 16h4.8l-.8 14h-3.2zM24 33.2a2.8 2.8 0 1 0 0 5.6 2.8 2.8 0 1 0 0-5.6z"},
};
static const IconLayer Icon_chevron[]={
    {'S',0,0,"M14.5 9l6-6 21 21-21 21-6-6 15-15z"},
};
static const IconLayer Icon_chevronLeft[]={
    {'S',0,0,"M33.5 9l-6-6-21 21 21 21 6-6-15-15z"},
};
static const IconLayer Icon_chevronDown[]={
    {'S',0,0,"M3 14.5l6-6 15 15 15-15 6 6-21 21z"},
};
static const IconLayer Icon_chevronUp[]={
    {'S',0,0,"M3 33.5l6 6 15-15 15 15 6-6-21-21z"},
};
static const IconLayer Icon_soundOn[]={
    {'S',0,0,"M4 16.5h8.5L25 6v36L12.5 31.5H4z"},
    {'B',3.5,0,"M30.5 17.5a8.5 8.5 0 0 1 0 13M35.5 11.5a16.5 16.5 0 0 1 0 25"},
};
static const IconLayer Icon_soundOff[]={
    {'S',0,0,"M4 16.5h8.5L25 6v36L12.5 31.5H4z"},
    {'S',0,0,"M29.5 18.5l3-3 5.5 5.5 5.5-5.5 3 3-5.5 5.5 5.5 5.5-3 3-5.5-5.5-5.5 5.5-3-3 5.5-5.5z"},
};
static const IconLayer Icon_star[]={
    {'S',0,0,"M24 4.5l4.9 13.2 14 .6-11 8.8 3.8 13.5L24 32.9l-11.7 7.7 3.8-13.5-11-8.8 14-.6z"},
};
static const IconLayer Icon_starOutline[]={
    {'H',0,0,"M24 4.5l4.9 13.2 14 .6-11 8.8 3.8 13.5L24 32.9l-11.7 7.7 3.8-13.5-11-8.8 14-.6zM24 14.7l2.5 6.8 7.2.3-5.6 4.5 1.9 6.9-6-3.9-6 3.9 1.9-6.9-5.6-4.5 7.2-.3z"},
};
static const IconLayer Icon_plus[]={
    {'S',0,0,"M20.5 5h7v15.5H43v7H27.5V43h-7V27.5H5v-7h15.5z"},
};
static const IconLayer Icon_minus[]={
    {'S',0,0,"M5 20.5h38v7H5z"},
};
// keyed by icon name
static const IconDef IconDefs[]={
    {"body",Icon_body,3},
    {"combat",Icon_combat,10},
    {"aim",Icon_aim,3},
    {"targets",Icon_targets,3},
    {"shield",Icon_shield,4},
    {"wrist",Icon_wrist,5},
    {"interface",Icon_interface,4},
    {"survival",Icon_survival,4},
    {"world",Icon_world,2},
    {"haptics",Icon_haptics,8},
    {"performance",Icon_performance,4},
    {"controls",Icon_controls,1},
    {"notes",Icon_notes,4},
    {"presets",Icon_presets,5},
    {"comfort",Icon_comfort,3},
    {"classic",Icon_classic,2},
    {"fullvr",Icon_fullvr,4},
    {"fast",Icon_fast,2},
    {"changed",Icon_changed,2},
    {"default",Icon_default,4},
    {"padlock",Icon_padlock,2},
    {"feel",Icon_feel,4},
    {"reset",Icon_reset,2},
    {"undo",Icon_undo,2},
    {"search",Icon_search,2},
    {"close",Icon_close,1},
    {"check",Icon_check,1},
    {"info",Icon_info,1},
    {"warning",Icon_warning,1},
    {"chevron",Icon_chevron,1},
    {"chevronLeft",Icon_chevronLeft,1},
    {"chevronDown",Icon_chevronDown,1},
    {"chevronUp",Icon_chevronUp,1},
    {"soundOn",Icon_soundOn,2},
    {"soundOff",Icon_soundOff,2},
    {"star",Icon_star,1},
    {"starOutline",Icon_starOutline,1},
    {"plus",Icon_plus,1},
    {"minus",Icon_minus,1},
};
