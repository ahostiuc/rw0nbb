/*
   ep_finalize.c

   event processing code for MJD
   - up to 16 digitizer modules for the Ge detectors, plus
   - QDCs etc for the veto

   David Radford   Nov 2016
*/
/* Close out processing of the events that have been handled by eventprocess()
   - write histograms to file
   - if there are enough data, also:
      - extract information on on-board trap offsets and thresholds, and tmax
      - extract information on baseline (E=0) trapfixed position and FWHM
      - extract data-cleaning limits (on resting baseline mean, RMS, and slope)
                and save the limits in a *.dcl file
      - extract live times from pulser counts

   returns: -1 on error, 0 otherwise
 */
/* Special version for deadtime evaluation July 2017
   Need starting and ending run, E=0 FWHM, true pos and neg thresholds, and pulser counts 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MJDSort.h"
#define HIS_COUNT    2200     // number of spectra in his[][] array

/*  ------------------------------------------------------------ */
/*  Use these definitions to adjust the function of this program */

#define VERBOSE    0
/*  ------------------------------------------------------------ */

float autopeak3(int *his, int lo, int hi, float *area_ret, float *fwhm_ret);

void find_limits(int *his, int lo, int hi, int *lolim, int *hilim) {
  // find limiting non-zero channels between chs lo and hi
  int i;
  for (i=lo; i<=hi && his[i]==0; i++);
  if (i > hi) return;  // all zeros
  *lolim = *hilim = i;
  for (i++; i<=hi; i++) if (his[i] > 0) *hilim = i;
  return;
}

/*  ------------------------------------------------------------ */

int ep_finalize(MJDetInfo *Dets, MJRunInfo *runInfo, int **his,
                int *on_bd_rise, int totevts, PTag  *pt) {

  int     i, j, k, n, t, chan, e, flag, s1, s2, pt_error;
  float   pos, area, fwhm, area2;
  float   tl, th, fl, th2, tl2, fh, rl, rh, rb;
  char    spname[256];
  FILE    *f_out1 = 0, *f_out2 = 0;

  static float e0pos[200], e0fwhm[200], bl[200], t1[200], obt_offset[200];

  /*
    detector channels:
         0 -  57:  Ge high gain
       100 - 157:  Ge low gain
       158 - 168:  pulser tag
    his channels:
         0 - 199   pulser-removed trapmax (4-2-4)
       200 - 399   pulser-tagged trapmax (4-2-4)
       400 - 599   dirty energy (e_ctc or trapmax) after data cleaning; ADC units
       600 - 799   clean energy (e_ctc or trapmax) after data cleaning; ADC units
       800 - 999   dirty energy (e_ctc or trapmax) after data cleaning; 0.5 keV/ch
      1000 - 1199  clean energy (e_ctc or trapmax) after data cleaning; 0.5 keV/ch
      1200 - 1399  A/E histograms
      1400 - 1599  DCR and lamda histograms
      1600 - 1799  baseline mean, RMS, and slope (+4000 when trapmax is < 50)
      1800 - 1999  tmax from trapmax (4-2-4) and 2000+t_1 (1% time)
                     and 6000+baseline_trap (E=0, 4-1.5-4) for FWHM, 0.05 ADC units
      2000 - 2199  trap difference (on-board - trapmax) for trapmax > 100
                     in bins of 0.01 (+1000) and 0.2 (+3000) ADC
                     and (t95 - t0) time difference for different A/E and DCR cuts 
   */

  printf("Writing his.rms\n"); fflush(stdout);
  f_out1 = fopen("his.rms", "w");
  for (i=0; i<HIS_COUNT && his[i] != 0; i++) {
    if (i == 95) {
      sprintf(spname, "%d; no-pulser built-event times [s]", i);
    } else if (i == 96) {
      sprintf(spname, "%d; pulser built-event times [s]", i);
    } else if (i == 97) {
      sprintf(spname, "%d; no-pulser ch-event times [s]", i);
    } else if (i == 98) {
      sprintf(spname, "%d; pulser ch-event times [s]", i);
    } else if (i == 99) {
      sprintf(spname, "%d; channel and crate ID stats", i);
    } else if (i < 200) {
      sprintf(spname, "%d; ch %d pulser-removed energy [ADC]; run %d", i, i, runInfo->runNumber);
    } else if (i < 400) {
      sprintf(spname, "%d; ch %d pulser-tagged energy [ADC]", i, i%200);
    } else if (i < 600) {
      sprintf(spname, "%d; ch %d dirty energy (e_ctc or trapmax) [ADC]", i, i%200);
    } else if (i < 800) {
      sprintf(spname, "%d; ch %d clean energy (e_ctc or trapmax) [ADC]", i, i%200);
    } else if (i < 1000) {
      sprintf(spname, "%d; ch %d dirty energy (e_ctc or trapmax) [0.5 keV]", i, i%200);
    } else if (i < 1200) {
      sprintf(spname, "%d; ch %d clean energy (e_ctc or trapmax) [0.5 keV]e", i, i%200);
    } else if (i < 1400) {
      sprintf(spname, "%d; ch %d A/E", i, i%200);
    } else if (i < 1600) {
      sprintf(spname, "%d; ch %d DCR and lamda", i, i%200);
    } else if (i < 1800) {
      sprintf(spname, "%d; ch %d baseline mean, RMS, slope (+4000 for e<50)", i, i%200);
    } else if (i < 2000) {
      sprintf(spname, "%d; ch %d trap_t_max and t_1 [10 ns] and E=0 [0.05 ADC]", i, i%200);
    } else {
      sprintf(spname, "%d; ch %d on-bd trap offset [0.01 & 0.2 ADC]", i, i%200);
    }
    write_his(his[i], 8192, i, spname, f_out1);
  }
  fclose(f_out1);

  /* presorted data subsets have few events, not enough to extract trap offsets,
     thresholds, data cleaning limits, etc... */
  if (totevts < 2000) return 0; // skip remaining end-of-run processing

  /*  ======  extract on-board trap offsets and thresholds, and tmax  ======  */
  if (Dets[0].HGcalib[0] > 0) {
    printf("\n"
           "                               On-bd                    True               T_1"
           "               On-bd                     True              T_1\n"
           "    Detector    rise  HGChan   offset  (keV)         threshold  (keV)     time"
           "     LGChan    offset  (keV)         threshold  (keV)     time\n");
  } else {
    printf("\n"
           "                               On-bd            True        T_1"
           "              On-bd            True        T_1\n"
           "    Detector    rise  HGChan   offset       threshold      time"
           "     LGChan   offset       threshold      time\n");
  }
  for (chan=0; chan < 100+runInfo->nGe; chan++) {
    obt_offset[chan] = -999;
    if (chan >= runInfo->nGe && chan < 100) continue;
    /* look for peak around bin 1000 (0.01 ADC) */
    if ((e = peak_find(his[2000+chan], 1, 1999))) { // peak found
      if ((pos = autopeak(his[2000+chan], e, &area, &fwhm)) == 0) pos = e;
      pos = 0.01 * (pos - 1000.0);
    } else {  /* look for peak above bin 4000 (0.1 ADC)*/
      if (!(e = peak_find(his[2000+chan], 2001, 7999)))  continue; // no peak found
      if ((pos = autopeak(his[2000+chan], e, &area, &fwhm)) == 0) pos = e;
      pos = 0.2 * (pos - 4000.0);
    }
    obt_offset[chan] = pos;
    // tmax = peak_find(his[1800+chan], 1, 1000); // find position of trap tmax peak
    if ((t = peak_find(his[1800+chan], 2001, 4000))) // find position of t_1 peak
      t1[chan] = autopeak(his[1800+chan], t, &area, &fwhm) - 2000.0;
  }
  for (chan=0; chan < runInfo->nGe; chan++) {
    if (!Dets[chan].HGChEnabled) continue;
    if (Dets[0].HGcalib[0] > 0) {
      printf("%8s %s %4d %6d", Dets[chan].DetName, Dets[chan].StrName, on_bd_rise[chan], chan);
      if (t1[chan] != 0) {
        printf("%9.2f (%5.2f) %7.2f ->%7.2f (%4.2f) %8.1f",
               obt_offset[chan], obt_offset[chan] * Dets[chan].HGcalib[0],
               Dets[chan].HGTrapThreshold/ (float) on_bd_rise[chan],
               Dets[chan].HGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[chan],
               (Dets[chan].HGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[chan]) * Dets[chan].HGcalib[0],
               t1[chan]);
      } else {
        printf("%51s", " ");
      }
      if (t1[100+chan] != 0) {
        printf("%10d %9.2f (%5.2f)  %7.2f ->%7.2f (%4.2f) %8.1f\n",
               chan+100, obt_offset[100+chan], obt_offset[100+chan] * Dets[chan].LGcalib[0],
               Dets[chan].LGTrapThreshold/ (float) on_bd_rise[chan],
               Dets[chan].LGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[100+chan],
               (Dets[chan].LGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[100+chan]) * Dets[chan].LGcalib[0],
               t1[100+chan]);
      } else {
        printf("\n");
      }
    } else { 
      printf("%8s %s %4d %6d", Dets[chan].DetName, Dets[chan%100].StrName, on_bd_rise[chan], chan);
      if (t1[chan] != 0) {
        printf("%9.2f %7.2f ->%7.2f %8.1f",
               obt_offset[chan],
               Dets[chan].HGTrapThreshold/ (float) on_bd_rise[chan],
               Dets[chan].HGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[100+chan],
               t1[chan]);
      } else {
        printf("%36s", " ");
      }
      if (t1[100+chan] != 0) {
        printf("%10d %9.2f %7.2f ->%7.2f %8.1f\n",
               chan+100, obt_offset[100+chan],
               Dets[chan].LGTrapThreshold/ (float) on_bd_rise[chan],
               Dets[chan].LGTrapThreshold/ (float) on_bd_rise[chan] - obt_offset[100+chan],
               t1[100+chan]);
      } else {
        printf("\n");
      }
    }
  }

  /*  ======  baseline (E=0) trapfixed position and FWHM ======  */
  for (i=0; i<200; i++) {
    e0pos[i] = e0fwhm[i] = bl[i] = t1[i] = 0;
  }

  printf("\n   NOTE that correct E=0 position and FWHM require 9us of flat signal baseline.\n");
  if (Dets[0].HGcalib[0] > 0) {
    printf("    Detector     HGChan   E=0  (keV)   FWHM   (keV)  area"
           "     LGChan   E=0  (keV)   FWHM   (keV)  area    diff\n");
  } else {
    printf("    Detector     HGChan   E=0  FWHM  area     LGChan   E=0  FWHM  area    diff\n");
  }
  fflush(stdout);
  for (chan=0; chan < runInfo->nGe; chan++) {
    if (!Dets[chan].HGChEnabled) continue;
    printf("%8s %s %6d", Dets[chan].DetName, Dets[chan%100].StrName, chan);
    // high gain
    if ((pos = autopeak2(his[1800+chan], 4000, 8000, &area, &fwhm)) &&
        area > 29) {   // note low required area for detectors w/o pulsers
      e0pos[chan] = 0.05 * (pos-6000);
      e0fwhm[chan] = 0.05 * fwhm;
      if (Dets[0].HGcalib[0] > 0) {
        printf("%7.2f (%5.2f) %5.2f (%5.3f) %5.0f", e0pos[chan], e0pos[chan] * Dets[chan].HGcalib[0],
               e0fwhm[chan], e0fwhm[chan] * Dets[chan].HGcalib[0], area);
      } else {
        printf("%7.2f %5.2f %5.0f", e0pos[chan], e0fwhm[chan], area);
      }
    } else {
      printf("%21s", " ");
      if (Dets[0].HGcalib[0] > 0) printf("%14s", " ");
    }
    // low gain
    if ((pos = autopeak2(his[1900+chan], 4000, 8000, &area2, &fwhm)) &&
        area2 > 100) {
      e0pos[100+chan] = 0.05 * (pos-6000);
      e0fwhm[100+chan] = 0.05 * fwhm;
      if (Dets[0].HGcalib[0] > 0) {
        printf("%10d %6.2f (%5.2f) %5.2f (%5.3f) %5.0f %7.0f\n",
               chan+100, e0pos[100+chan], e0pos[100+chan] * Dets[chan].LGcalib[0],
               e0fwhm[100+chan], e0fwhm[100+chan] * Dets[chan].LGcalib[0], area2, area-area2);
      } else {
        printf("%10d %6.2f %5.2f %5.0f %7.0f\n",
               chan+100, e0pos[100+chan], e0fwhm[100+chan], area2, area-area2);
      }
    } else {
      printf("\n");
    }
  }
  printf("\n");

  /* ====== extract live times from pulser counts ====== */

  /* ptag.nevts[chan][0]: energy-ungated HG pulser cts
     ptag.nevts[chan][1]: energy-ungated LG pulser cts
     ptag.nevts[chan][2]: energy-ungated HG&&LG pulser cts
     ptag.nevts[chan][3]: energy-gated HG pulser cts
     ptag.nevts[chan][4]: energy-gated LG pulser cts
     ptag.nevts[chan][5]: energy-gated HG&&LG pulser cts
     ptag.nevts[chan][6]: expected energy-ungated pulser cts (from finding max over the CC)
   */
  for (i=0; i < runInfo->nCC+1; i++) {
    printf("CC %d of %d: dets ", i, runInfo->nCC);
    // find max # pulser counts between all channels on a given controller card
    k = 0;
    for (j=0; j < runInfo->nGe; j++) {
      if (Dets[j].CCnum == i) printf(" %d", j);
      n = pt->nevts[j][0] + pt->nevts[j][1] - pt->nevts[j][2]; // count for either HG or LG
      if (j != 16 && Dets[j].CCnum == i && k < n) k = n;
    }
    //  and save in pt->nevts[][6] (expected # pulser counts)
    for (j=0; j < runInfo->nGe; j++)
      if (Dets[j].CCnum == i) pt->nevts[j][6] = k;
    printf("\n");
  }
  
#ifndef QUIET
  /* report results */
  printf("\n"
         "                           Pulser Counts                         LiveTime\n"
         "        Detector        HG    LG  Both  Expected     diff      Either   (Both)\n");
  fflush(stdout);
  for (chan=0; chan < runInfo->nGe; chan++) {
    if (!Dets[chan].HGChEnabled ||   // ch not enabled
        pt->nevts[chan][6] < 2 ||   // no pulser expected
        (pt->nevts[chan][0] == 0 && pt->nevts[chan][1] == 0)) continue;  // no pulser
    n = pt->nevts[chan][0] + pt->nevts[chan][1] - pt->nevts[chan][2]; // either
    printf("%3d %8s %s %6d %5d %5d %7d %6d %4d %10.4f  (%6.4f)\n",
           chan, Dets[chan].DetName, Dets[chan%100].StrName,
           pt->nevts[chan][0], pt->nevts[chan][1],
           pt->nevts[chan][2], pt->nevts[chan][6],
           pt->nevts[chan][6] - pt->nevts[chan][0],
           pt->nevts[chan][6] - pt->nevts[chan][1],
           (float) n / (float) pt->nevts[chan][6],
           (float) pt->nevts[chan][2] / (float) pt->nevts[chan][6]);
  }
#endif

  for (i=0; i < runInfo->nGe; i++) {
    if (Dets[i].HGcalib[0] < 0.1) Dets[i].HGcalib[0] = 1.0;
    if (Dets[i].LGcalib[0] < 0.1) Dets[i].LGcalib[0] = 1.0;
  }

  /* ===== make a file listing deadtimes, thresholds, FWHM(E=0) ===== */
  if (!(f_out2 = fopen("DT_vs_thresh.dat", "w"))) return 0;
  fprintf(f_out2,
          "#                      HG                              LG\n"
          "#ID pos "
          "  FWHM thr_pos  thr_neg  dead%%  "
          "  FWHM thr_pos  thr_neg  dead%%  "
          "dead_OR%% Detector     Pulser counts\n");
  fflush(stdout);
  for (i=0; i < runInfo->nGe; i++) {
    if (Dets[i].HGChEnabled || Dets[i].LGChEnabled) {
      th  = Dets[i].HGTrapThreshold / (float) on_bd_rise[i] - obt_offset[i];
      tl  = Dets[i].LGTrapThreshold / (float) on_bd_rise[i] - obt_offset[i+100];
      th2 = Dets[i].HGTrapThreshold / (float) on_bd_rise[i] + obt_offset[i];
      tl2 = Dets[i].LGTrapThreshold / (float) on_bd_rise[i] + obt_offset[i+100];
      fh = e0fwhm[i];
      fl = e0fwhm[i+100];
      n = pt->nevts[i][0] + pt->nevts[i][1] - pt->nevts[i][2]; // either
      rh = 100.0 * (float) (pt->nevts[i][6] - pt->nevts[i][0]) / (float) pt->nevts[i][6];
      rl = 100.0 * (float) (pt->nevts[i][6] - pt->nevts[i][1]) / (float) pt->nevts[i][6];
      rb = 100.0 * (float) (pt->nevts[i][6] - n) / (float) pt->nevts[i][6];

      // check that pulser tag is still working for this channel
      pt_error = s1 = s2 = 0;
      for (j=pt->elo[i]; j<=pt->ehi[i]; j++) s1 += his[i][j];
      for (j=pt->elo[i+100]; j<=pt->ehi[i+100]; j++) s2 += his[100+i][j];
      if ((s1 > 10 && rh > 2.0) || (s2 > 10 && rl > 2.0)) {
        pt_error = -999;
        rh = 100.0 * (float) (pt->nevts[i][6] - pt->nevts[i][0]-s1) / (float) pt->nevts[i][6];
        rl = 100.0 * (float) (pt->nevts[i][6] - pt->nevts[i][1]-s2) / (float) pt->nevts[i][6];
        rb = -9.99;
      }

      if (obt_offset[i] < -998) {
        th = th2 = 0;
        rh = rb = -9.99;
      }
      if (obt_offset[i+100] < -998) {
        tl = tl2 = 0;
        rl = rb = -9.99;
      }
      if (fh < 0.001) rh = rb = -9.99;
      if (fl < 0.001) rl = rb = -9.99;

      fprintf(f_out2, "%3d %c%c%c  %5.2f %7.2f %7.2f %7.2f "
                               "  %5.2f %7.2f %7.2f %7.2f "
              " %7.2f   %s %8d %7d %7d %7d %d %d\n",
              i, Dets[i].StrName[1], Dets[i].StrName[3], Dets[i].StrName[5],
              fh, th, th2, rh, fl, tl, tl2, rl, rb, Dets[i].StrName,
              pt->nevts[i][0], pt->nevts[i][1], n, pt->nevts[i][6], s1+s2, pt_error);
    }
  }
  fclose(f_out2);  // FIXME: this causes crash on ORNL cluster!!!

  /* finally check for cases where, for the pulser, we count both > either channel by itself;
     or the energy gate misses a large fraction of the events.
     This can happen when there's lots of noise */
  /* ptag.nevts[chan][0]: energy-ungated HG pulser cts
     ptag.nevts[chan][1]: energy-ungated LG pulser cts
     ptag.nevts[chan][2]: energy-ungated HG&&LG pulser cts
     ptag.nevts[chan][3]: energy-gated HG pulser cts
     ptag.nevts[chan][4]: energy-gated LG pulser cts
     ptag.nevts[chan][5]: energy-gated HG&&LG pulser cts
     ptag.nevts[chan][6]: expected energy-ungated pulser cts (from finding max over the CC)
   */
  flag = 0;
  for (j=0; j < runInfo->nGe; j++) {
    if (pt->nevts[j][2] > pt->nevts[j][0] ||
        pt->nevts[j][2] > pt->nevts[j][1])
      printf("Error in pulser counts for detector %d; Both > HG or Both > LG\n", j);
    if ((j != 16 || !SPECIAL_DET_16) &&
        (pt->nevts[j][3] < pt->nevts[j][0]*4/5 ||
         pt->nevts[j][4] < pt->nevts[j][1]*4/5)) flag = j;
  }
 
  if (flag) {
    printf("\n >>>>>>  WARNING: The pulser tag is having problems with the energy gates.  <<<<<<\n"
           "   The energies may have changed, or the resolution may be a lot worse for\n"
           "       some detectors than what is given in the .pdt file.\n"
           "   Consider re-running pulser_tag_init, or discarding some detectors for this run.\n"
           "   Problem detectors are: ");
    for (j=0; j < runInfo->nGe; j++) {
      if (j != 16 &&
          (pt->nevts[j][3] < pt->nevts[j][0]*4/5 ||
           pt->nevts[j][4] < pt->nevts[j][1]*4/5))
        printf("%3d", j);
    }
    printf("\n");
  }

  return 0;  // END of post-run processing

} /* ep_finalize() */
