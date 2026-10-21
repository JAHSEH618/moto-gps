# Rides and Performance Runs are separate records

**Status:** accepted

A Ride is a navigation session, recorded automatically from navigation start to end. A Performance Run is a single acceleration attempt and can happen with or without an active Ride. They are stored and listed separately; a run may reference the Ride it happened during. Recording every motion as a Ride was rejected because the user wants navigation sessions as trip history, while acceleration attempts still need to be captured.
