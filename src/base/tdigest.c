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

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "tdigest.h"
#include "tree.h"
#include <assert.h>
#include <stdio.h>
#define arc4random_uniform(x) (rand() % x)

struct Centroid {
    RB_ENTRY(Centroid) entry;
    double mean;
    size_t count;
};

int centroidcmp(Centroid *c1, Centroid *c2)
{
    return (c1->mean < c2->mean ? -1 : 1);
}

struct TDigest {
    RB_HEAD(CentroidTree, Centroid) C;
    size_t count;
    size_t ncentroids;
    double delta;
    unsigned int K;
    size_t ncompressions;
};

RB_GENERATE(CentroidTree, Centroid, entry, centroidcmp)

TDigest* TDigest_create(double delta, unsigned int K)
{
    TDigest *digest = calloc(1, sizeof(TDigest));
    RB_INIT(&(digest->C));
    digest->delta = delta;
    digest->K = K;
    digest->count = 0;
    digest->ncentroids = 0;
    digest->ncompressions = 0;
    return digest;
}

void TDigest_destroy(TDigest* digest)
{
    Centroid *c, *nxt;

    for (c = RB_MIN(CentroidTree, &(digest->C)); c != NULL; c = nxt) {
        nxt = RB_NEXT(CentroidTree, &(digest->C), c);
        RB_REMOVE(CentroidTree, &(digest->C), c);
        free(c);
    }
    free(digest);
}

TDigest * TDigest_add(TDigest *pdigest, double x, size_t w)
{
    TDigest *rd = NULL;
    Centroid *cj;
    cj = TDigest_find_closest_centroid(pdigest, x, w);

    if (cj != NULL) {
        // Add the data point to the selected centroid.
        RB_REMOVE(CentroidTree, &((pdigest)->C), cj);
        Centroid_add(cj, x, w);
        RB_INSERT(CentroidTree, &((pdigest)->C), cj);
    } else {
        Centroid *c = Centroid_create(x, w);
        RB_INSERT(CentroidTree, &((pdigest)->C), c);
        pdigest->ncentroids += 1;
    }

    (pdigest)->count += w;

    if ((pdigest)->ncentroids > (pdigest)->K / (pdigest)->delta) {
        rd = TDigest_compress(pdigest);
    }
    return rd;
}

Centroid *TDigest_find_closest_centroid(TDigest *digest, double x, size_t w)
{
    // Find all the centroids whose mean is the closest to x. Return the number
    // of centroids that are closest to x.
    if (digest->ncentroids == 0) {
        return NULL;
    }
    
    double z, min_distance = DBL_MAX;
    double sum = 0.0;
    Centroid *c, *lower_closest, *upper_closest = NULL;
    lower_closest = RB_MIN(CentroidTree, &(digest->C));

    // Start at the beginning of the tree keep going as long as the distance to
    // x decreases.
    for (c = lower_closest; c != NULL; c = RB_NEXT(CentroidTree, &(digest->C), c)) {
        // Sum the counts of centroids with mean smaller than x. This is used
        // to compute the quantile.
        if (c->mean < x) {
            sum += c->count;
        }
        z = fabs(c->mean - x);
        if (z < min_distance) {
            min_distance = z;
            lower_closest = c;
        } else if (z > min_distance) {
            upper_closest = c;
            break;
        }
    }
     
    // Start at the lower_closest and choose one of the closer centroids at
    // random.
    double qc, threshold;
    double n = 0.0;
    Centroid *closest = NULL;

    for (c = lower_closest; c != upper_closest; c = RB_NEXT(CentroidTree, &(digest->C), c)) {
        qc = (c->count / 2.0 + sum) / digest->count;
        sum += c->count;
        threshold = 4 * digest->count * digest->delta * qc * (1 - qc);
        if (c->count + w <= threshold) {
            n++;
            if (rand() / (double)RAND_MAX < 1.0 / n) {
                closest = c;
            }
        }
    }
    return closest;
}

TDigest * TDigest_compress(TDigest *digestp)
{
    TDigest *new_digest = TDigest_create(digestp->delta, digestp->K);
    Centroid *c;
    int i, j;
    while (!RB_EMPTY(&(digestp->C))) {
        j = arc4random_uniform(digestp->ncentroids);
        c = RB_MIN(CentroidTree, &(digestp->C));
        for (i = 0; i < j; i++) {
            c = RB_NEXT(CentroidTree, &(digestp->C), c);
        }
        RB_REMOVE(CentroidTree, &(digestp->C), c);
        digestp->count -= c->count;
        digestp->ncentroids -= 1;
        TDigest * next_digest = TDigest_add(new_digest, c->mean, c->count);
        if (next_digest != NULL) {
            fprintf(stdout, "TDigest_compress recursive\n");
            TDigest_destroy(new_digest);
            new_digest = next_digest;
            assert(0);
        }
        free(c);
    }

    new_digest->ncompressions = digestp->ncompressions;
    new_digest->ncompressions++;
    return new_digest;
}
 
size_t TDigest_get_ncompressions(TDigest *digest)
{
    return digest->ncompressions;
}

double TDigest_percentile(TDigest *digest, double q)
{
    double delta, t = 0;
    bool first = true;
    Centroid *c;
    q *= digest->count;
    RB_FOREACH(c, CentroidTree, &(digest->C)) {
        if (q < t + c->count) {
            if (first) {
                return c->mean;
            } else if (c == RB_MAX(CentroidTree, &(digest->C))) {
                return c->mean;
            } else {
                double dprev = c->mean - RB_PREV(CentroidTree, &(digest->C),c )->mean;
                double dnext = RB_NEXT(CentroidTree, &(digest->C), c)->mean - c->mean;
                delta = (dprev < dnext ? 2*dprev : 2*dnext);
            }
            return c->mean + ((q - t) / c->count - 0.5) * delta;
        }
        t += c->count;
        first = false;
    }
    return RB_MAX(CentroidTree, &(digest->C))->mean;
}

size_t TDigest_get_ncentroids(TDigest *digest)
{
    return digest->ncentroids;
}

Centroid *TDigest_get_centroid(TDigest *digest, size_t i)
{
    Centroid *c = RB_MIN(CentroidTree, &(digest->C));
    size_t j;

    for (j = 0; j < i; j++) {
        c = RB_NEXT(CentroidTree, &(digest->C), c);
    }
    
    return c;
}

size_t TDigest_get_count(TDigest *digest)
{
    return digest->count;
}

Centroid* Centroid_create(double x, size_t w)
{
    Centroid *centroid = malloc(sizeof(Centroid));
    centroid->count = w;
    centroid->mean = x;
    return centroid;
}

void Centroid_add(Centroid *c, double x, size_t w)
{
    c->count += w;
    c->mean += w * (x - c->mean) / c->count;
}

double Centroid_quantile(Centroid *c, TDigest *digest)
{
    Centroid *cj;
    double quantile = c->count / 2.0;
    for (cj = RB_PREV(CentroidTree, &(digest->C), c); cj != NULL; cj = RB_PREV(CentroidTree, &(digest->C), cj)) {
        quantile += cj->count;
    }
    return quantile / digest->count;
}

double Centroid_get_mean(Centroid *c)
{
    return c->mean;
}

size_t Centroid_get_count(Centroid *c)
{
    return c->count;
}

