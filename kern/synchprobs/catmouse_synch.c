#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/**
	Global synchronization declarations
*/
 // sem_count should be the total bowl count
static struct semaphore *eating;

// Use when switching from one species to another
static struct lock *speciesSwitch;

// Use when trying to lock down a bowl
static struct lock *myBowl;
static struct cv *myBowlIsAvailable;

struct species {

	// Are there members of this species that want to eat?
	volatile bool wantToEat;

	// Number of creatures of this species that are or will be eating shortly
	// This must reach 0 before a new type of creature is chosen
	volatile unsigned int numEating;

	// conditional signaling that this species is done eating for now
	struct cv *doneEating;
};

// The two species (this can potentially be arbitrary)
struct species *cats;
struct species *mice;

// Array symbolizing how bowls are being used
// bowlUsage[i] should be true if a bowl is in use, and false otherwise
volatile bool *bowlUsage;

// helper function declarations
void before_eating(struct species *, struct species *, unsigned int);
void after_eating(struct species *, struct species *, unsigned int);

/*
	The CatMouse simulation will call this function once before any cat or mouse
	tries to each.

	You can use it to initialize synchronization and other variables.

	parameters: the number of bowls
*/
void catmouse_sync_init(int bowls) {
	// replace this default implementation with your own implementation of
	// catmouse_sync_init

	eating = sem_create("eating", bowls);
	if (eating == NULL) {
		panic("could not create global eating synchronization semaphore");
	}

	speciesSwitch = lock_create("speciesSwitch");
	if (speciesSwitch == NULL) {
		panic("could not create global speciesSwitch synchronization lock");
	}

	myBowl = lock_create("myBowl");
	if (myBowl == NULL) {
		panic("could not create global myBowl synchronization lock");
	}
	myBowlIsAvailable = cv_create("myBowlIsAvailable");
	if (myBowlIsAvailable == NULL) {
		panic("could not create global myBowlIsAvailable synchronization cv");
	}

	// Initialize bowl usage array
	bowlUsage = kmalloc(bowls * sizeof(bool));
	for (int i = 0; i < bowls; i++) {
		bowlUsage[i] = false;
	}

	// Allocate cats and mice species data
	cats = kmalloc(sizeof(struct species));
	if (cats == NULL) {
		panic("could not create global cats species structure");
	}

	mice = kmalloc(sizeof(struct species));
	if (mice == NULL) {
		panic("could not create global mice species structure");
	}

	cats->wantToEat = false;
	mice->wantToEat = false;
	cats->numEating = 0;
	mice->numEating = 0;

	cats->doneEating = cv_create("catsDoneEating");
	if (cats->doneEating == NULL) {
		panic("could not create conditional variable catsDoneEating");
	}
	mice->doneEating = cv_create("miceDoneEating");
	if (mice->doneEating == NULL) {
		panic("could not create conditional variable miceDoneEating");
	}

	return;
}

/*
	The CatMouse simulation will call this function once after all cat
	and mouse simulations are finished.

	You can use it to clean up any synchronization and other variables.

	parameters: the number of bowls
*/
void catmouse_sync_cleanup(int bowls) {
	// replace this default implementation with your own implementation of
	// catmouse_sync_cleanup
	(void)bowls;  // keep the compiler from complaining about unused parameters

	KASSERT(eating != NULL);
	KASSERT(speciesSwitch != NULL);

	sem_destroy(eating);
	lock_destroy(speciesSwitch);
	lock_destroy(myBowl);

	if (bowlUsage != NULL) {
		kfree((void *)bowlUsage);
		bowlUsage = NULL;
	}
	if (cats != NULL) {
		kfree((void *)cats);
		cats = NULL;
	}
	if (mice != NULL) {
		kfree((void *)mice);
		mice = NULL;
	}
}

void before_eating(struct species *mySpecies, struct species *otherSpecies, unsigned int bowl) {

	// Don't queue any more of this creature if the opposite creature wants to eat

	lock_acquire(speciesSwitch);
		while (otherSpecies->wantToEat) {
			cv_wait(otherSpecies->doneEating, speciesSwitch);
		}
		mySpecies->wantToEat = true;
		while (otherSpecies->numEating > 0) {
			cv_wait(otherSpecies->doneEating, speciesSwitch);
		}
		cv_broadcast(otherSpecies->doneEating, speciesSwitch);
	lock_release(speciesSwitch);

	mySpecies->numEating++;

	// Now it's my species turn
	// Wait until my bowl is available, if required
	lock_acquire(myBowl);
		while (bowlUsage[bowl - 1]) {
			cv_wait(myBowlIsAvailable, myBowl);
		}
		bowlUsage[bowl - 1] = true;
	lock_release(myBowl);

	P(eating);
	// Now we eat!
}

void after_eating(struct species *mySpecies, struct species *otherSpecies, unsigned int bowl) {

	V(eating); // done eating

	// done eating, let others wanting to use my bowl know that it's available
	lock_acquire(myBowl);
		bowlUsage[bowl - 1] = false;
		cv_broadcast(myBowlIsAvailable, myBowl);
	lock_release(myBowl);

	mySpecies->wantToEat = false;
	mySpecies->numEating--;
	cv_signal(mySpecies->doneEating, speciesSwitch); // anybody else want to eat?

	if (otherSpecies->wantToEat) {
		// Wait until this species is fully done eating before letting them sleep
		lock_acquire(speciesSwitch);
			while (mySpecies->numEating > 0) {
				cv_wait(mySpecies->doneEating, speciesSwitch);
			}
			cv_signal(mySpecies->doneEating, speciesSwitch);
		lock_release(speciesSwitch);
	}

}

/*
	The CatMouse simulation will call this function each time a cat wants
	to eat, before it eats.
	This function should cause the calling thread (a cat simulation thread)
	to block until it is OK for a cat to eat at the specified bowl.

	parameter: the number of the bowl at which the cat is trying to eat
		legal bowl numbers are 1..NumBowls

	return value: none
*/
void cat_before_eating(unsigned int bowl) {
	before_eating(cats, mice, bowl);
}

/*
	The CatMouse simulation will call this function each time a cat finishes
	eating.

	You can use this function to wake up other creatures that may have been
	waiting to eat until this cat finished.

	parameter: the number of the bowl at which the cat is finishing eating.
		legal bowl numbers are 1..NumBowls

	return value: none
*/
void cat_after_eating(unsigned int bowl) {
	after_eating(cats, mice, bowl);
}

/*
	The CatMouse simulation will call this function each time a mouse wants
	to eat, before it eats.
	This function should cause the calling thread (a mouse simulation thread)
	to block until it is OK for a mouse to eat at the specified bowl.

	parameter: the number of the bowl at which the mouse is trying to eat
		legal bowl numbers are 1..NumBowls

	return value: none
*/
void mouse_before_eating(unsigned int bowl) {
	before_eating(mice, cats, bowl);
}

/*
	The CatMouse simulation will call this function each time a mouse finishes
	eating.

	You can use this function to wake up other creatures that may have been
	waiting to eat until this mouse finished.

	parameter: the number of the bowl at which the mouse is finishing eating.
		legal bowl numbers are 1..NumBowls

	return value: none
*/
void mouse_after_eating(unsigned int bowl) {
	after_eating(mice, cats, bowl);
}
