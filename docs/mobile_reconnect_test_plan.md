# Mobile Reconnect Test Plan

## Common scenario
1. connect/login
2. select world
3. select character
4. enter world
5. receive `zone_map_state`
6. move once
7. force disconnect or application pause
8. reconnect
9. re-login and re-enter, or resume token flow when implemented

## Mobile-like cases
- app pause/resume
- socket close while app stays alive
- wifi/LTE handoff approximation via disconnect + reconnect
- duplicate/stale session cleanup observation

## Current practical path
Resume-token based seamless recovery is not fully implemented yet.
The current verifiable path is reconnect + re-login + re-enter.
