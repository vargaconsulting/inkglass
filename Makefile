all:
	g++ -o x11_overlay main.cpp -lX11 -lXfixes -lcairo -lXext
