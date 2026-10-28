"""Special move names for KI1 (arcade), by char id, from the MKKomplete
Killer Instinct (1994) move list:
https://icemantraveler.github.io/mkksg/non_mk/killerinstinct/1/default.htm

Each entry: (name, motion in catalog.py's notation, buttons). Motion tokens
are directions relative to facing (B* / F* = hold back / forward, i.e. a
charge). Buttons: "P" any punch, "K" any kick, or a list such as
"LP MP". The first entry that matches a command's motion and button wins,
so specific buttons come before "P"/"K".
"""

NAMES = {
    1: [  # Jago
        ("Endokuken", "D DF F", "P"),
        ("Tiger Fury", "F D DF", "P"),
        ("Laser Sword", "DF D DB", "HP"),
        ("Wind Kick", "DF D DB", "K"),
        ("Wind Kick", "DB D DF", "LK"),
    ],
    2: [  # TJ Combo
        ("Spinfist", "B* F", "LP"),
        ("Turn Around Punch", "F* B", "LP"),
        ("Rollercoaster", "B* F", "MP"),
        ("Powerline", "B* F", "HP"),
        ("Knee KO", "B* F", "MK"),
        ("Flying Knee", "B* F", "LK HK"),
    ],
    3: [  # Spinal
        ("Super Searing Skull", "B DB D DF F", "P K"),
        ("Searing Skull", "D DF F", "P"),
        ("Bone Shaker", "F N F", "P"),
        ("Bone Shaker", "F N F N", "P"),
        ("Skeleport (front)", "D N D", "P"),
        ("Skeleport (behind)", "D N D", "K"),
        ("Skeleport (front)", "D N D N", "P"),
        ("Skeleport (behind)", "D N D N", "K"),
        ("Soul Sword", "B* F", "MP"),
        ("Soul Sword", "F* B", "MP"),
        ("Sliding Kick", "DB D DF", "HK"),
    ],
    4: [  # Chief Thunder
        ("Sammamish", "F DF D DB B", "HP"),
        ("Phoenix", "D DF F", "K"),
        ("Tomahawk", "D DB B", "HP"),          # in the air
        ("Flying Mohawk", "D DB B", "P"),
        ("Triplax", "B* F", "P"),
        ("Reverse Triplax", "F* B", "MP"),
    ],
    5: [  # Glacius
        ("Ice Lance", "DF D DB", "LP"),
        ("Cold Shoulder", "B* F", "P"),
        ("Shockwave", "D DF F", "P"),
        ("Liquidize", "D DF F", "LK"),
        ("Liquidize Uppercut", "D DF F", "MK HK"),
    ],
    6: [  # Fulgore
        ("Laser Storm (triple)", "F B N B D DF F", "LP"),
        ("Laser Storm (double)", "B N B D DF F", "LP"),
        ("Laser Storm", "D DF F", "P"),
        ("Plasma Slice", "F D DF", "P"),
        ("Plasma-port (front)", "B D DB", "P"),
        ("Plasma-port (behind)", "B D DB", "K"),
        ("Reflect", "D DB B", "P"),
        ("Eye Laser", "DF D DB", "HK"),
        ("Cyber Dash", "B* F", "K"),
    ],
    7: [  # Cinder
        ("Heatsink", "F DF D DB B", "HP"),
        ("Mirage", "F DF D DB B", "MP"),
        ("Heat Fist", "B N B", "LP"),
        ("Inferno", "F N F", "K"),
        ("Inferno", "B* F", "K"),
        ("Trailblazer", "B* F", "P"),
        ("Trailblazer", "F N F", "P"),
        ("Flip Kick", "F D DF", "K"),
    ],
    8: [  # Sabrewulf
        ("Flaming Bat", "D DB B", "P"),
        ("Sabre-Spin", "B* F", "P"),
        ("Reverse Sabre-Spin", "F* B", "MP"),
        ("Sabre-Roll", "B* F", "LK"),
        ("Sabre-Slap", "B* F", "MK"),
        ("Sabre-Pounce", "B* F", "HK"),
        ("Howl", "DF D DB", "HK"),
    ],
    9: [  # B. Orchid
        ("Lasaken", "D DF F", "P"),
        ("Flick Flak", "B* F", "K"),
        ("Fire Cat", "B* F", "P"),
        ("Spinning Sword", "DF D DB", "HP"),
        ("Ichi", "DF D DB", "LP MP"),
    ],
    10: [  # Riptor
        ("Claw Uppercut", "DF D DB", "LP"),
        ("Dragon Breath", "DF D DB", "HP"),
        ("Tail Flip", "DF D DB", "K"),
        ("Flaming Venom", "D DB B", "P"),
        ("Rampage", "B* F", "P"),
        ("Jump Rake", "B* F", "K"),
        ("Reverse Jump Rake", "F* B", "LK MK"),
    ],
    11: [  # Eyedol
        ("Fireball", "D DF F", "P"),
        ("Head Charge", "B* F", "LP"),
        ("Club Swing", "B* F", "HP"),
        ("Leaping Mace (forward)", "B* F", "LK"),
        ("Leaping Mace (upward)", "B* F", "MK"),
        ("Leaping Mace (backward)", "B* F", "HK"),
    ],
}

PUNCHES, KICKS = {"LP", "MP", "HP"}, {"LK", "MK", "HK"}


def special_name(c, motion, button):
    """name for a special with this motion (e.g. "DF D DB") and button"""
    for name, m, btns in NAMES.get(c, []):
        if m != motion:
            continue
        ok = set()
        for b in btns.split():
            ok |= PUNCHES if b == "P" else KICKS if b == "K" else {b}
        if button in ok:
            return name
    return None
