compile:
	gcc -Wall -Wextra -pedantic-errors pideShop.c -lpthread -lrt -o PideShop -lm
	gcc -Wall -Wextra -pedantic-errors hungryVeryMuch.c -lpthread -lrt -o HungryVeryMuch -lm

clean:
	rm PideShop && rm HungryVeryMuch
