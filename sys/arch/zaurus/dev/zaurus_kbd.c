/* $OpenBSD: zaurus_kbd.c,v 1.1 2005/01/13 20:47:57 drahn Exp $ */
/*
 * Copyright (c) 2005 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <arm/xscale/pxa2x0reg.h>
#include <arm/xscale/pxa2x0_gpio.h>


int
gpio_sense_pins_c3000[] = {
	12,
	17,
	91,
	34,
	36,
	38,
	39,
	-1
};

int
gpio_strobe_pins_c3000[] = {
	88,
	23,
	24,
	25,
	26,
	27,
	52,
	103,
	107,
	-1,
	108,
	114
};



struct zkbd_softc {
	struct device sc_dev;

	int *sc_sense_array;
	int *sc_strobe_array;
	int sc_nsense;
	int sc_nstrobe;

	int sc_onkey_pin;
	int sc_sync_pin;
	int sc_swa_pin;
	int sc_swb_pin;
	char *sc_okeystate;
	char *sc_keystate;
};

int zkbd_match(struct device *, void *, void *);
void zkbd_attach(struct device *, struct device *, void *);

int zkbd_irq(void *v);
int zkbd_on(void *v);
int zkbd_sync(void *v);
int zkbd_hinge(void *v);

struct cfattach zkbd_ca = {
	sizeof(struct zkbd_softc), zkbd_match, zkbd_attach
};
struct cfdriver zkbd_cd = {
	NULL, "zkbd", DV_DULL
};



int
zkbd_match(struct device *parent, void *cf, void *aux)
{
	return 1;
}


void
zkbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct zkbd_softc *sc = (struct zkbd_softc *)self;
	int pin, i;

	/* Determine which system we are - XXX */

	if (1 /* C3000 */) {
		sc->sc_sense_array = gpio_sense_pins_c3000;
		sc->sc_strobe_array = gpio_strobe_pins_c3000;
		sc->sc_nsense = sizeof(gpio_sense_pins_c3000)/sizeof(int);
		sc->sc_nstrobe = sizeof(gpio_strobe_pins_c3000)/sizeof(int);
		sc->sc_onkey_pin = 95;
		sc->sc_sync_pin = 16;
		sc->sc_swa_pin = 97;
		sc->sc_swb_pin = 96;
	} /* XXX */

	sc->sc_okeystate = malloc((sc->sc_nsense * sc->sc_nstrobe),
	    M_MBUF, M_NOWAIT);
	sc->sc_keystate = malloc((sc->sc_nsense * sc->sc_nstrobe),
	    M_MBUF, M_NOWAIT);

	/* set all the strobe bits */
	for (i = 0; i < sc->sc_nstrobe; i++) {
		pin = sc->sc_strobe_array[i];
		if (pin == -1) {
			continue;
		}
		pxa2x0_gpio_set_function(pin, GPIO_SET|GPIO_OUT);
	}
	/* set all the sense bits */
	for (i = 0; i < sc->sc_nsense; i++) {
		pin = sc->sc_sense_array[i];
		if (pin == -1) {
			continue;
		}
		pxa2x0_gpio_set_function(pin, GPIO_IN);
		pxa2x0_gpio_intr_establish(pin, IST_EDGE_BOTH, IPL_BIO,
		    zkbd_irq, sc, sc->sc_dev.dv_xname);
	}
	pxa2x0_gpio_intr_establish(sc->sc_onkey_pin, IST_EDGE_RISING, IPL_BIO,
	    zkbd_on, sc, sc->sc_dev.dv_xname);
	pxa2x0_gpio_intr_establish(sc->sc_sync_pin, IST_EDGE_RISING, IPL_BIO,
	    zkbd_sync, sc, sc->sc_dev.dv_xname);
	pxa2x0_gpio_intr_establish(sc->sc_swa_pin, IST_EDGE_BOTH, IPL_BIO,
	    zkbd_hinge, sc, sc->sc_dev.dv_xname);
	pxa2x0_gpio_intr_establish(sc->sc_swb_pin, IST_EDGE_BOTH, IPL_BIO,
	    zkbd_hinge, sc, sc->sc_dev.dv_xname);
}

/* XXX only deal with keys that can be pressed when display is open? */
/* XXX are some not in the array? */
/* handle keypress interrupt */
int
zkbd_irq(void *v)
{
	struct zkbd_softc *sc = v;
	int i, col;
	int pin;

	/* discharge all */
	for (i = 0; i < sc->sc_nstrobe; i++) {
		pin = sc->sc_strobe_array[i];
		if (pin != -1) {
			pxa2x0_gpio_clear_bit(pin);
			pxa2x0_gpio_set_dir(pin, GPIO_IN);
		}
	}

	delay (10);
	for(col = 0; col < sc->sc_nstrobe; col++) {
		if (sc->sc_strobe_array[i] == -1)
			continue;

		pin = sc->sc_strobe_array[col];

		/* activate_col */
		pxa2x0_gpio_set_bit(pin);
		pxa2x0_gpio_set_dir(pin, GPIO_OUT);

		/* wait activate (and discharge, overlapped) delay */
		delay (10);

		/* read row */
		for (i = 0; i < sc->sc_nsense; i++) {
			if (sc->sc_sense_array[i] == -1) 
				continue;

			sc->sc_keystate [i + (col * sc->sc_nsense)] =
			    pxa2x0_gpio_get_bit(sc->sc_sense_array[i]);
		}

		/* reset_col */
		pxa2x0_gpio_set_dir(pin, GPIO_IN);
	}

	printf("zkbd_irq\n");
	for (i = 0; i < (sc->sc_nsense * sc->sc_nstrobe); i++) {
		if (sc->sc_okeystate[i] != sc->sc_keystate[i]) {
			if (sc->sc_keystate[i]) 
				printf("key %d pressed\n", i);
			else
				printf("key %d released\n", i);
		}
		sc->sc_okeystate[i] = sc->sc_keystate[i];
	}

	/* charge all */
	for (i = 0; i < sc->sc_nstrobe; i++) {
		pin = sc->sc_strobe_array[i];
		if (pin != -1) {
			pxa2x0_gpio_set_bit(pin);
			pxa2x0_gpio_set_dir(pin, GPIO_OUT);
		}
	}

	/* force the irqs to clear as we have just played with them. */
	for (i = 0; i < sc->sc_nsense; i++)
		if (sc->sc_sense_array[i] != -1)
			pxa2x0_gpio_clear_intr(sc->sc_sense_array[i]);

	return 1;
}

int
zkbd_on(void *v)
{
	printf("on key pressed\n");
	return 1;
}

int
zkbd_sync(void *v)
{
	printf("sync button pressed\n");
	return 1;
}

int
zkbd_hinge(void *v)
{
	printf("hing event pressed\n");
	return 1;
}
