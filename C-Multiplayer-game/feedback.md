# Feedback on pac-man exercise :

## Clarity of task definition
Task was defined as a **Multiplayer (using posix sockets) Pac-man game over LAN in C language on Linux** (+ **Without** using the internet for until server code was said to be freeze).

How the game looks like can be seen on Youtube etc.
No requirements document was supplied since this is the first time that such task was given (and thus was flexible)  

## Steps / Directions
- I was instructed to start from the **server** and design of the communication/application **protocol** (which I thought can be defined by a state machine), and only afterwards implement the client (starting from a **"dumb" automatic client** )
- It was kind of **hard to check the correctness/ how easy it is to implement my protocol in code** without first having some kind of a mock client that communicates with the server process.
- First part I had to test was to make sure the client and server communicate correctly and that each of them advance their state within their own state-machines.
Then it became possible to check the correctness of the game logic in the server and to identify initial bugs.
- In the beginning, for easier debugging - I've inserted the restrictions on sending client's move commands within the client.   
Eventually I were told to make a reasonable change which was to  **put the main/business logic in the server** in such a way that a client can send as much movement commands as he wants, but the server decides how and when to accept them.
- Once the game logic worked fine enough for an automatic player (went through enough tests), it was time to add support for human player (controlled by keyboard) and screen rendering on client side (trial and error without using some kind of FPS formula other than knowing that the client's update sampling frequency should be at least x2 times the server updates publishing frequency)  
- Choosing **whether to implement the client in C or other language** (i.e python / C++) was left to me to decide, but since most of the logic was already built in C (Server & client) , and since it seemed to be the most portable option (which won't require setup modifications like having root permissions or GUI backend) I picked C with built in library termios.h .
- Since the system features can be added endlessly, perhaps some **list of mandatory features** that has most of the added value in regarding to learning and solving complex problems should have been predefined by a senior colleague.  
Even though no limitations on implementing the solution itself might lead to different restrictions and challenges, I guess there will be **some common software pitfalls / challenges** that can be given as a "food for thought"/**open questions/warnings ahead of time.** [see Code Gotchas]

## Complexities / Struggles
### Forbidden to use resources other then man pages
- For some logic I wished to implement but didn't know what tools to use, man -k / apropos wasn't always getting me to what I looked for (unlike an internet search with some keywords).
As far as I've checked, the main way to search offline for text within the actual content of man pages in a range of/all man sections is with **man -K** , which is kind of helpful but might feel like looking for a needle in haystack (many results).
- Some man pages didn't include examples and weren't so clear (I wasn't sure which combination of functions and options/flags I needed, so sometimes I had to do trial and error.
- Code editor (vscode) bugs and/or compiler bugs which are hard to solve without using the internet.
- Sometimes losing some work due to not having enough experience with git
- Although external resources (Books etc.) are not 100% reliable, they might help to understand the unclear stuff from the man pages (which are also not perfect or comprehensive enough). 
### No easy to use built in library for linked lists
- sys/queue.h uses cryptic macros , using arrays instead makes the code more bug prone and rigid.
### Failure to separate the server threads logic into different files
- my intention was to make it easier to inspect the code (which thread can enter what functions) 
- Couldn't compile it and had a lot of variables to share between them.
- Mainly got "multiple definition of" functions and variables/defines (const char vars among them) and undeclared errors
### Freezing the server code
- some bugs and design flaws may be found only in late stages, although not being able to modify the server requires the a lot of sophistication in the client side for troubleshooting , it might be too much pain for no good reason.
### Testing
- I haven't successfully implemented yet a script that runs a simulation with a lot of clients to test for bugs (I did it semi-automatic).



## What I've learned
- Games development (even "simple" ones like this) can be quite complex and involves a lot of low level design decisions.  
- Having intuition about the quality and/or intention of other people's undocumented code is possible only after self experimenting with a subject (i.e DIY first approach).
- Working in a modular fashion (small pieces of undependable code) allows for better flexibility when changes are need to be made.
-  
- 

## Things which I think can be improved
### Motivation for the task
- other than reaching a state of working game, the fun of playing it and gaining experience in low level development (in a do it yourself manner), It didn't mention any connection to software security and/or real-life tools, although it seemed like at least the "termios.h" part is the basis of how terminals work (i.e SSH and tty) and might have some relationship to security. 
### Supervision & Guidance
- More frequent short code reviews in order to make sure I focus on the critical parts and understood correctly the task. It can also help in spotting weak spots or knowledge gaps
- Encouraging senior workers to offer some help from time to time
- Regarding demonstration of methodology for approaching similar development challenges by senior staff (how to read documentation, low level development etc.) - I don't think that would be smart use of their time while there are at least two reputable sources online in the from of comprehensive videos
	- Geohot channel (George Hotz) [Link](https://www.youtube.com/watch?v=7Hlb8YX2-W8&list=PLzFUMGbVxlQs5s-LNAyKgcq5SL28ZLLKC)
	- Gamozo labs) [Link](https://www.youtube.com/c/gamozolabs/videos)

### Software development
- Giving ahead some guidelines on how to approach the task (SDLC principles, testing etc.)
- How to distinct between bad smell code and a good one?
- Giving a methodology for avoiding code duplication other than encapsulation in functions. 
- Encouraging inspection of the relevant functions  in Linux source code (the parts which are not visible from within the code editor) in order to gain better understanding after reading the information in the man pages (maybe seeing the implementation with the comments will help to clear doubts of why things don't work as expected)


## Code Gotchas - What can go wrong when implementing/coding the system
### Server
- If server runs on WSL,  I couldn't find a way to remotely connect to it from another host. (need to run it on a VM / native linux / RPI etc.) 
- Would you choose TCP or UDP ?
- How would you handle **multiple clients "at the same time"** (Although multicore/multiprocessing isn't needed) without blocking while keeping quality of service / not blocking the server.
- Would you use a single thread to handle all clients asynchronously or a thread for each client ? (b.t.w - I'm not sure it is even possible to execute all the server tasks with a single thread ) 
- Would you use multiple processes for the server or a single one? 
- How would you store the state of the players , the game board and it's objects (cells)
-  How would you move the objects (such as monsters) on the board? (if each monster was represented by a thread, then more synchronization was needed. but if it is implemented serially, then no need for locking)
- how would you synchronize between accepting clients move commands, updating the game state (which is basically ought to be controlled by a timer) , and sending/pushing updates to the subscribed clients?
- Would you collect messages from clients in a message-queue like structure and parse/use them only every x usec or 
- How much data can a client sent before the socket buffer gets full and cause a write to it block / fail if the server won't get to read it soon enough? does sockets have separate raw bytes buffers for read and write? (like read & write fd for pipes) Same goes for the case when server sends updates to a client which won't read it.
- Is there a limit (MTU like) on size of data chunk that can be written in one go to a socket? (for example when we try to send the whole board chars to clients.  

### Client
- Would you use multiple processes for the server or a single one? what are the tradeoffs?
- How would you listen to keyboard events while listening for server updates at the same time? would you schedule the checking for updates with a timer or handle it asynchronously?
- Is raw terminal mode the only way to get keyboard presses?
- How bad is it to enter and exit raw terminal mode after every keyboard hit event? (vs. staying there until game ends)
- what methods are available to inform the raw terminal mode thread about a game over other than sharing memory? (Is it possible to send signals to such thread from another thread? I don't think so)  
- How fast does the keyboard samples a constantly pressed button? is it fast enough compared to the speed of which the server moves the monsters? is it under our control (baud rate etc.)? is there any advantage for supporting this vs keep sending the last given direction to the server until a different one is detected? 
- Note that client listens asynchronously to keyboard events, therefore if you choose to implement receiving updates asynchronously also, then you might get into troubles if you try to define a timeout on the update receiving part (to detect a game over / disconnection)
- 
### General / common
- why does sigaction doesn't work with pthread_signal but does work with sigwait()? 
- Is there a limit on how many built in timers of the same type you can use at the same time? (I think each of them invokes a different signal, but their time measurement method is very different)
- beware of nested interrupts/signals when setting recurring timers.
-   Is your code portable enough so that server & client are agnostic to the OS architecture they are running on? (i.e 32bit vs 64 bit)
> Written with [StackEdit](https://stackedit.io/).