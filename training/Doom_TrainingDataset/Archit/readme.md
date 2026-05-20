# Archit's Training Data

## Overview
- **Levels Played:** Episode 1, Maps 1 through 5 (E1M1 - E1M5)
- **Difficulty:** Hard (Skill 4)
- **Data Formats:** Includes both `.csv` (raw feature/action logs) and `.lmp` (visual demo replays for debugging).

## Training Methodology
When recording this data, I intentionally played in a way that provides high-quality examples for the AI to learn from:

- **Different Routes (E1M1 Focus):** I recorded 3 separate runs of E1M1, taking a slightly different path to the exit each time. This prevents the bot from strictly memorizing one single route.
- **Getting Unstuck:** I intentionally walked very close to walls and then smoothly steered away. This teaches the bot the corrective actions needed if it gets snagged on a corner.
- **Mixed Combat:** I varied my engagement distances. In one run, I pushed close to monsters with the shotgun; in another, I engaged from further away.
- **Smooth Movement:** I made a conscious effort to keep my keyboard and turning inputs as smooth as possible to prevent erratic, noisy training data.
- **Skipped Secrets:** I ignored hidden rooms and focused entirely on forward progression so the bot learns to prioritize finishing the level.
