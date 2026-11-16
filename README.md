# Amalgam

This is my chess engine that I started after being somewhat satisfied with [gilperchess](https://github.com/gilp06/gilperchess). However, there were a few changes I wanted to make that made me want to rewrite it in C++. I mainly wanted to use Disservin's [chess-library](https://github.com/Disservin/chess-library) to handle the board-state/making moves since its performance and support for Chess960 is much better than my original attempt. As you can probably guess, the name of the engine comes from it being an amalgamation of sorts from various engine and search ideas from other places that I put into here. 

# NNUE

Currently the NNUE network is the last selfplay one from gilperchess. In short, it was initially trained off of the WDL of the Lichess Elite Database (The idea came from the Viridithas dev as I remember reading somewhere that it made their engine play more aggressively which I thought was cool). Afterward, it has been trained on self-play games using that initial and subsequent network. At some point, I want to restart a new model from the database just so I have a clean slate to work with. 

