# Misson Code: Duo

## Author

Cheyu Tu

## Design

> What is new and interesting about this game?

This is a two-player game. Players are secret agents investigating a restaurant to uncover hidden clues crucial to their next mission. One player will provide instruction, the other deciphers it to locate the hidden items.

## Networking

> How is the client/server multiplayer in this game? What messages are transmitted? Where in the code?

1. Role Selection

Only one player can be the Communicator, and the other must be the Operative.

In the start (Lobby) scene, each player uses the Up/Down keys to select a role and presses Enter to confirm they are ready.

Specific logic:

- When Player 1 (P1) switches their role selection, the current choice (selected_role_1) is sent to the server using
send_selected_role_message defined in Game.hpp/cpp.

- When P1 confirms a role (e.g., selects Communicator and presses Enter), the client sends the chosen role enum (Role::Communicator) to the server using send_login_message in Game.hpp/cpp.

- On the server, role_1 is updated to P1’s confirmed role.

- If Player 2 (P2) has already selected the same role (e.g., Communicator) and is ready, the server forces P2 to switch to the complementary role (Operative).

In this case: selected_role_2 is set to Operative, and P2’s ready state (role_2) is reset to Unknown.

- Players remain in the Lobby until both are ready with complementary roles (Communicator ↔ Operative).

- When that condition is met, the server proceeds the game to the Communication phase.

- Each Lobby snapshot from the server includes role_1, role_2, selected_role_1, and selected_role_2.

- On the client side, if the local player is ready but the other is not, the “Log In” button label changes to
“Waiting for your teammate…”.


2. Instruction message

- During the Communication phase, the Communicator can send instructions.

- When the Communicator finishes typing and presses Enter: 

    - The instruction text is sent to the server using send_instruction_message in Game.hpp/cpp.

    - The game phase changes from Communication to Operation.

- The server then:

    - Stores the original instruction in instruction_text.

    - Creates a corrupted copy (corrupted_instruction) by replacing 50% of non-space characters with "*" to simulate damaged communication.

    - Sends the new game state to both clients.

- In the Operation phase, both clients display the corrupted instruction instead of the original message.

## Screen Shot:

![Screen Shot](Screenshot_2025-10-07.png)

## How To Play:

> describe the controls and (if needed) goals/strategy.

1. Start a local server:
```
./dist/server 30000
```

2. Connect to the server as player 1:
```
./dist/client localhost 30000
```

3. In a new terminal window, connect to the server as player 2:
```
./dist/client localhost 30000
```

4. In the start scene, use up and down arrows to select a role. Hit Enter when you are ready.

5. When both players are ready, they will see the restaurant scene. 

6. The communicator will see five objects will be highlighted in a distinct color (I haven't implemeted this yet). They are the target objects. It's time for the communicator to describe what those objects are. Press Enter to send the instruction message when done.

7. 50% of thhe instruction message will be corrupted (replaced by "*"). The operator's job is to locate the target objects from the scene within limited attempts based on the corrupted instruction.

8. The operator can click on the scene to select an object. (I only partially implemented the object collision. If you click on the little guy on the left bottom corner, you should see a "hit Adult.001" in the terminal).

9. If the operator finds all target objects within 25 clicks, game success. (I haven't implemented this yet).


## Sources:

> list a source URL for any assets you did not create yourself. Make sure you have a license for the asset.

- https://www.blenderkit.com/get-blenderkit/7666f504-12b6-4015-a19e-57f451cd7bf4/
- https://www.blenderkit.com/get-blenderkit/8f413ad2-6328-4f89-b401-c9b497273408/
- https://www.blenderkit.com/get-blenderkit/46076e7d-530b-43a1-8b81-f103745a4b29/
- https://fonts.google.com/specimen/Courier+Prime?preview.layout=grid&query=courier

This game was built with [NEST](NEST.md).