0. server boots up and waits for players
1.players connect to the server and decide between 3 options (join as active/passive player to current game ,wait for others ,start game)

Once a game was started:
Server creates another THREE process/thread for:
- Rendering the game board in-memory 
  (needs to wait each time for 2nd process to finish sending the update
  before determining the next board state)
- Sending the game board to all players (BROADCAST?)
- Recieving movements from players & handling connection requests on the listening socket 

Clients will have two threads/processes:
- one for reading game updates and displaying it on screen
- Second for sending movements/commands to the server within a game 