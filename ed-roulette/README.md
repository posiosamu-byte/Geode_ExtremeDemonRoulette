# Extreme Demon Roulette (Geode)

Demon roulette for Geometry Dash. Pick a list, get random levels, and hit a rising percent on each one.

## How it plays

1. Press the new **play-style button** in the main menu's bottom row.
2. Choose **AREDL** or **Rated+**.
3. You get a random level in your rank range and a goal (starts at 1%, goes up by 1% per level).
4. Reach the goal in normal mode (practice and testing don't count), then quit the level.
5. Pass and you get the next level. Miss the goal and the run is over.

## Settings

Rank range, starting percent, percent increase, AREDL legacy levels, and the two API URLs.

## Data sources

- AREDL: `https://api.aredl.net/v2/api/aredl/levels`
- Rated+: `https://pointercrate.com/api/v2/demons/listed/?rated_plus=true`  **(unverified)**

## Building

geode build

5.11.0


