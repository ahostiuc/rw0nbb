# Makefile for data analysis

HOME = "/Users/fy2/code2/"

all_files = PZ.input skim.dat ctc.rms psa.rms

All: $(all_files)

PZ.input: Data
	$(HOME)PZcal Data; mv PZ.output PZ.input

skim.dat: PZ.input
	$(HOME)skim Data

ctc.rms: skim.dat
	$(HOME)CTcal skim.dat; mv ctc.output ctc.input; mv gains.output gains.input

psa.rms: skim.dat ctc.rms ctc.input gains.input
	$(HOME)PSAcal skim.dat; mv psa.output psa.input

