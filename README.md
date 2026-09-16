# Conscious

This is the Conscious Project, a try to investigate if artificial consciousness can arise from proprioception, i.e., integration by a being of a model of itself (and the world itself) in the way that the being interacts with its world.

We will start by enumarating some elements that we will need:
- An open, modular and distributed architecture: e.g., the world could be implemented in C and be a shared memory segment, and the beings could be written in smalltalk so they can evolve and coexist with other differently-evolved beings in the same engine.
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