# Conscious

This is the Conscious Project, a try to investigate if artificial consciousness can arise from proprioception, i.e., integration by a being of a model of itself (and the world itself) in the system that the being uses to interact with its world.

## Modules

- [conscious (main module and time sync)](modules/conscious/README.md)

## Requirements and architecture brainstorm

We will start by enumerating some elements that we will need:
- An open, modular and distributed architecture:
    - e.g., the world could be implemented in C and be a shared memory segment, and the beings could be written in smalltalk so they can evolve and coexist with other differently-evolved beings in the same engine.
- Time engine
    - which allows the world and beings (and other ones) engines to exist synchronizedly.
    - able to be suspended for debug, accelerated, slowed down, etc.
- World: is a repository and an engine
    - probably with several dimensions: not only one object or characteristic in each cell (food and its growth, surface of water, difficulty to walk due to grass growth, etc.).
    - quantized: in cells, with perhaps different cell size per dimension.
    - could run based on rules: for example, the grass grows every time tic
- Beings: is a repository and and engine
    - object oriented, with own rules
    - able to evolv in some way in the engine, so different specifimens with different features (DNA) and feature expressions can exist simultaneously
    - with features as
        - able to emit messages
        - basic circuits (thirst, hunger, reproduction, cold...) in the reality and the model
        - lifecycle (reproduction)
        - integrated model of the world and of the being itself (not necessarily synchronized)
        - with surviving instict as the "spark" that keeps them alive
        - with behaviours that triggers "anomalies", like curiosity, etc.
- Some kind of snapshot feature of the different repositories/engines
    - For backup/restore, debug, make reports, be inspected by humans...
    - That can be used by external tools to "translate to human language" what happens in the world and inside the beings (evolution, "thoughts", analysis of protolanguage...)
- World console or "control panel": Inspection/interpretation framework:
    - Allows for suspending, explaining, explain changes between snaps, translating the world, beings, etc.
    - Real time (graphical or textual to feed IA agents) representation of the state of the world, beings, etc.
- Debugging capabilities:
    - Compatible with the distributed and possible heterogeneous paradigm (C, Java, Smalltalk...)
- Maintaing compatibility by abstraction and layers/interfaces/endpoints

## Requirements backlog

- How to automate build and execution
- Graphical representation: world console or "control panel"
- How to deal with different versions and compatibility amoung engines:
    - Versions, tags, releases, documenting compatibility and features, requirements...
- Setup and understanding of the Github issues subsystem.

## References

- [Project in Github](https://github.com/marianoalda/conscious)
- [ChatGPT conversation including implementation and ethics](https://chatgpt.com/share/6aaab1ce-9c64-83ed-971f-411bacf42cb4)
- [Gemini conversation including the own basic theory about artificial consciousness](https://share.gemini.google/azB9mMfjqQD8)