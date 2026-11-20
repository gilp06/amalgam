# Amalgam

This is my chess engine that I started after being somewhat satisfied with [gilperchess](https://github.com/gilp06/gilperchess). However, there were a few changes I wanted to make that made me want to rewrite it in C++. I mainly wanted to use Disservin's [chess-library](https://github.com/Disservin/chess-library) to handle the board-state/making moves since its performance and support for Chess960 is much better than my original attempt. As you can probably guess, the name of the engine comes from it being an amalgamation of sorts from various engine and search ideas from other places that I put into here. 

Currently, to build/run it you will need a processor that supports PEXT and AVX-512.

# NNUE

The NNUE development on this engine is pretty similar to my previous one.

- led-base-1024: (768->1024)x2->1, trained on the WDL of the Lichess Elite Database, with roughly 1.8B unique positions. I chose to do this as I had read that the Viridithas dev used this for the initial network (albeit rescored by HCE), and that it produced a more aggressive engine, so I was curious if even the WDL would do something similar.
- datagen-v1-256: (768->256)x2->1, trained on 2M selfplay games using led-base-1024 with 8 plies of random moves at the start and 5k softnodes

# References
- [OpenBench](https://github.com/AndyGrant/OpenBench) - Used for a lot; i.e. SPRT and selfplay PGN generation.
- [bullet](https://github.com/jw1912/bullet) - Training library used for the NNUE.
- [incbin](https://github.com/graphitemaster/incbin) - used for embedding the NNUE weights into the engine at compile time.
- [Lichess Elite Database](https://database.nikonoel.fr/) - as mentioned prior, this was used for training data. I combined the 2013-2020 release along with the remaining 2020-2025 individual downloads
- [pgn2binpack](https://github.com/Disservin/pgn2binpack) and [pawnocchio](https://github.com/JonathanHallstrom/pawnocchio) for converting and analyzing binpacks
- A couple really nice engines that I have referred to when working on the project that were are [Ethereal](github.com/AndyGrant/Ethereal), [Viridithas](https://github.com/cosmobobak/viridithas), and [Stockfish](https://github.com/official-stockfish/Stockfish).
- Chess programming wiki and Engine Programming discord