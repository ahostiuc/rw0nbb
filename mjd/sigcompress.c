/*  ------------------------------------------------------------ */

int compress_signal(short *sig_in, unsigned short *sig_out, int sig_len_in) {

  int   i, j, max1, max2, min1, min2, ds, nb1, nb2;
  int   iso, nw, bp, dd1, dd2;
  unsigned short db[2];
  unsigned int   *dd = (unsigned int *) db;
  static unsigned short mask[17] = {0, 1,3,7,15, 31,63,127,255,
                                    511,1023,2047,4095, 8191,16383,32767,65535};

  //static int len[17] = {4096, 2048,512,256,128, 128,128,128,128,
  //                      128,128,128,128, 48,48,48,48};
  /* ------------ do compression of signal ------------ */
  j = iso = bp = 0;

  sig_out[iso++] = sig_len_in;     // signal length
  while (j < sig_len_in) {         // j = starting index of section of signal
    // find optimal method and length for compression of next section of signal 
    max1 = min1 = sig_in[j];
    max2 = -16000;
    min2 = 16000;
    nb1 = nb2 = 2;
    nw = 1;
    for (i=j+1; i < sig_len_in && i < j+48; i++) { // FIXME; # 48 could be tuned better?
      if (max1 < sig_in[i]) max1 = sig_in[i];
      if (min1 > sig_in[i]) min1 = sig_in[i];
      ds = sig_in[i] - sig_in[i-1];
      if (max2 < ds) max2 = ds;
      if (min2 > ds) min2 = ds;
        nw++;
    }
    if (max1-min1 <= max2-min2) { // use absolute values
      nb2 = 99;
      while (max1 - min1 > mask[nb1]) nb1++;
      //for (; i < sig_len_in && i < j+len[nb1]; i++) {
      for (; i < sig_len_in && i < j+128; i++) { // FIXME; # 128 could be tuned better?
        if (max1 < sig_in[i]) max1 = sig_in[i];
        dd1 = max1 - min1;
        if (min1 > sig_in[i]) dd1 = max1 - sig_in[i];
        if (dd1 > mask[nb1]) break;
        if (min1 > sig_in[i]) min1 = sig_in[i];
        nw++;
      }
    } else {                      // use difference values
      nb1 = 99;
      while (max2 - min2 > mask[nb2]) nb2++;
      //for (; i < sig_len_in && i < j+len[nb1]; i++) {
      for (; i < sig_len_in && i < j+128; i++) { // FIXME; # 128 could be tuned better?
        ds = sig_in[i] - sig_in[i-1];
        if (max2 < ds) max2 = ds;
        dd2 = max2 - min2;
        if (min2 > ds) dd2 = max2 - ds;
        if (dd2 > mask[nb2]) break;
        if (min2 > ds) min2 = ds;
        nw++;
      }
    }

    if (bp > 0) iso++;
    /*  -----  do actual compression  -----  */
    sig_out[iso++] = nw;  // compressed signal data, first byte = # samples
    bp = 0;               // bit pointer
    if (nb1 <= nb2) {
      /*  -----  encode absolute values  -----  */
      sig_out[iso++] = nb1;                    // # bits used for encoding
      sig_out[iso++] = (unsigned short) min1;  // min value used for encoding
      for (i = iso; i <= iso + nw*nb1/16; i++) sig_out[i] = 0;
      for (i = j; i < j + nw; i++) {
        dd[0] = sig_in[i] - min1;              // value to encode
        dd[0] = dd[0] << (32 - bp - nb1);
        sig_out[iso] |= db[1];
        bp += nb1;
        if (bp > 15) {
          sig_out[++iso] = db[0];
          bp -= 16;
        }
      }

    } else {
      /*  -----  encode derivative / difference values  -----  */
      sig_out[iso++] = nb2 + 32;  // # bits used for encoding, plus flag
      sig_out[iso++] = (unsigned short) sig_in[j];  // starting signal value
      sig_out[iso++] = (unsigned short) min2;       // min value used for encoding
      for (i = iso; i <= iso + nw*nb2/16; i++) sig_out[i] = 0;
      for (i = j+1; i < j + nw; i++) {
        dd[0] = sig_in[i] - sig_in[i-1] - min2;     // value to encode
        dd[0]= dd[0] << (32 - bp - nb2);
        sig_out[iso] |= db[1];
        bp += nb2;
        if (bp > 15) {
          sig_out[++iso] = db[0];
          bp -= 16;
        }
      }
    }
    j += nw;
  }

  if (bp > 0) iso++;
  if (iso%2) iso++;     // make sure iso is even for 4-byte padding
  return iso;           // number of shorts in compressed signal data

} /* compress_signal */

/*  ------------------------------------------------------------ */

int decompress_signal(unsigned short *sig_in, short *sig_out, int sig_len_in) {

  int   i, j, min, nb, isi, iso, nw, bp, siglen;
  unsigned short db[2];
  unsigned int   *dd = (unsigned int *) db;
  static unsigned short mask[17] = {0, 1,3,7,15, 31,63,127,255,
                                    511,1023,2047,4095, 8191,16383,32767,65535};

  /* ------------ do decompression of signal ------------ */
  j = isi = iso = bp = 0;
  siglen = (short) sig_in[isi++];  // signal length
  //printf("<<< siglen = %d\n", siglen);
  for (i=0; i<2048; i++) sig_out[i] = 0;
  while (isi < sig_len_in && iso < siglen) {
    if (bp > 0) isi++;
    bp = 0;              // bit pointer
    nw = sig_in[isi++];  // number of samples encoded in this chunk
    nb = sig_in[isi++];  // number of bits used in compression

    if (nb < 32) {
      /*  -----  decode absolute values  -----  */
      min = (short) sig_in[isi++];  // min value used for encoding
      db[0] = sig_in[isi];
      for (i = 0; i < nw && iso < siglen; i++) {
        if (bp+nb > 15) {
          bp -= 16;
          db[1] = sig_in[isi++];
          db[0] = sig_in[isi];
          dd[0] = dd[0] << (bp+nb);
        } else {
          dd[0] = dd[0] << nb;
        }
        sig_out[iso++] = (db[1] & mask[nb]) + min;
        bp += nb;
      }

    } else {
      nb -= 32;
      /*  -----  decode derivative / difference values  -----  */
      sig_out[iso++] = (short) sig_in[isi++];  // starting signal value
      min = (short) sig_in[isi++];             // min value used for encoding
      db[0] = sig_in[isi];
      for (i = 1; i < nw && iso < siglen; i++) {
        if (bp+nb > 15) {
          bp -= 16;
          db[1] = sig_in[isi++];
          db[0] = sig_in[isi];
          dd[0] = dd[0] << (bp+nb);
        } else {
          dd[0] = dd[0] << nb;
        }
        sig_out[iso] = (db[1] & mask[nb]) + min + sig_out[iso-1]; iso++;
        bp += nb;
      }
    }
    j += nw;
  }

  if (siglen != iso) {
    printf("ERROR in decompress_signal: iso (%d ) != siglen (%d)!\n",
           iso, siglen);
  }
  return siglen;       // number of shorts in decompressed signal data

} /* decompress_signal */
