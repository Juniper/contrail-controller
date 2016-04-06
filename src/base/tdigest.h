/*
Copyright (c) 2015 Loïc Séguin-Charbonneau

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/*
 * This is an implementation of the t-digest algorithm by Ted Dunning and Otmar
 * Ertl. The algorithm is detailed in https://github.com/tdunning/t-digest/blob/master/docs/t-digest-paper/histo.pdf
 * and a reference implementation in Java is available at https://github.com/tdunning/t-digest.
 *
 * The current implementation has also been inspired by the work of Cam
 * Davidson-Pilon: https://github.com/CamDavidsonPilon/tdigest.
 */

#ifndef TDIGEST_H
#define TDIGEST_H

#define DEFAULT_DELTA 0.01
#define DEFAULT_K 100

typedef struct TDigest TDigest;
typedef struct Centroid Centroid;

TDigest *TDigest_create(double delta, unsigned int K);
void TDigest_destroy(TDigest* digest);
TDigest * TDigest_add(TDigest *pdigest, double x, size_t w);
Centroid *TDigest_find_closest_centroid(TDigest *digest, double x, size_t w);
TDigest * TDigest_compress(TDigest *digest);
double TDigest_percentile(TDigest *digest, double q);
size_t TDigest_get_ncentroids(TDigest *digest);
Centroid *TDigest_get_centroid(TDigest *digest, size_t i);
size_t TDigest_get_ncompressions(TDigest *digest);
size_t TDigest_get_count(TDigest *digest);
double Centroid_quantile(Centroid *c, TDigest *digest);
Centroid* Centroid_create(double x, size_t w);
void Centroid_add(Centroid *c, double x, size_t w);
double Centroid_get_mean(Centroid *c);
size_t Centroid_get_count(Centroid *c);

#endif
