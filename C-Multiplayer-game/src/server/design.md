# Server
## Roles
### Mandatory
- Manages current game state (Scores,players,Board,Alive/dead players)
- publish live game view to the clients(active players and spectators) (+including the scores)
  - Might want to send a MULTICASE/BROADCAST update every X useconds
- Manages connections/file descriptors representing each connected player
- Move monsters in automatic manner (dumb)
- For now , we will support one live match at any moment.
### Optional 
- Manages the scoring database (history)
- Logging module
  
# Clients
## Roles / Features
### Mandatory
- Connect to a given ip/hostname of the server
- decide wether to create a new game or join an existing one (as active / spectator) 
- Send movement commands to the server once they are in a game (termios.h)
- Be able to exit a game and/or logout at any time without crashing the server or current match
- Can't send any other input/commands to the server
- Can't cheat


# Game Logic
If a game is currently played: 
---------------------------------------
- new players can either wait for the next match, join the current one or create new one if doesn't exist

Otherwise
----------------------------------------
players can decide if they want to start a new match or wait
for other players to join. (use a default timer for handling synchronization issues)


# Implementation
## Threading (thread for each client vs single thread to handle all clients)
Since we are not dealing with worker threads and SIMD style jobs,
I don't see any advantage of using multithreading.
On the other hand, creating a process for each player might also be not ideal.
Since we are dealing with human interface, We don't need the performance benefits of multicore/multiprocess programming

Players (might) share the same game, but each of them has it's own state (which should be kept on the server) 
Therefore (Sharing memory between procesess is more expensive/complicated.
On the other hand - threads synchronization is prone to synchronization problems)

## Processes/Threads on server
### Accepting new clients
- start from 2 players minimum
- Each accepted client will result in a new socket file descriptors being creating and added to an epoll() mechanism (epoll())
- Another thread/process will be in charge of pushing periodic game updates to the players who are registered to the current match
  - (publisher subscriber pattern)
  - pthread_cond in client

## Processes/Threads on client side
### Rendering game state on screen
- display the current board state + score on screen (live,event based/timer/interrupt based)
### Sending player movements to the server ( event/interrupt based)
- sending the pressed key as data over the client socket


# Possible problems
- BLOCKing vs nonblocking sockets
- If messages are handled in an Out of order manner, game result is not as expected
- Lags
- forgeting to set the Maximum socket receive buffer size
- 
TODO: check when a fork is done within the following syscalls
TODO: choose between select,poll,epoll(single thread) and pthread (multithreaded)

Order of syscalls:

1.Create socket for incoming connections
1. bind()
2. listen()

infinite loop():
4. accept (inside a loop)

ORDER OF IMPORTANCE:

1. get the clients to connect to the server
2. Display the welcome screen + status of current game
3. Show options to wait/logout/start game
4. display scores