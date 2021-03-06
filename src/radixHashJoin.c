#include "radixHashJoin.h"

void testRHJ() {

    int cache_size = 6 * (1024 * 1024); // Cache size is 6mb
    // n  = cache size / maxsizeofbucket;
    
    Result *result;
    int i;
    /* H1_PARAM is the number of the last-n bits of the 32-bit number we wanna keep */
    int32_t n = H1_PARAM;

    /* So we are going to use mod(%2^n) to get the last n bits, where 2^n is also the number of buckets */
    number_of_buckets = (int32_t) myPow(2, n);

    threadpool = threadpool_init(number_of_buckets);

    Relation **relations = myMalloc(sizeof(Relation *) * 10);
    uint32_t relation_size[10] = {200, 100, 30, 10, 10, 10, 10, 10, 10, 10};

    /* Allocate the relations and initialize them with random numbers from 0-200 */
    for (i = 0; i < 10; i++) {
        relations[i] = allocateRelation(relation_size[i], TRUE);    // Allocation-errors are handled internally.
        initializeRelationWithRandomNumbers(&relations[i]);
    }

    /* Do Radix Hash Join on the conjunction of the relations */
    result = RadixHashJoin(&relations[0], &relations[1]);
    printResults(result);

    threadpool_destroy(threadpool);

    for (i = 0; i < 10; i++)
        if (relations[i] != NULL)
            deAllocateRelation(&relations[i]);

    deAllocateResult(&result);
    free(relations);
}


/* Relation R: reIR, Relation S: reIS, Number of buckets: 2^n */
Result *RadixHashJoin(Relation **reIR, Relation **reIS) {

    Result *result;
    int i;

    //printf("\n# Running RadixHashJoin on Relations R and S.\n");
#if PRINTING || DEEP_PRINTING
    clock_t start_t, end_t, total_t;
    start_t = clock();
#endif

/*    //	Certain values to create chains *//*
    (*reIR)->tuples[(*reIR)->num_tuples - 2].payload = 929;
    (*reIR)->tuples[(*reIR)->num_tuples - 3].payload = 828;
    (*reIS)->tuples[(*reIS)->num_tuples - 1].payload = 929;
    (*reIS)->tuples[(*reIS)->num_tuples - 2].payload = 20;*/

    /*     Construct Histograms and Psums      */
    /*   Create Relation R histogram and psum  */
    int32_t **histogramR = createHistogram((*reIR));    // Allocation-errors are handled internally.

    //printHistogram(histogramR, 1);
    if ((*reIR)->psum == NULL) {
        (*reIR)->psum = createPsum(histogramR);    // Allocation-errors are handled internally.
    } else {
        //printf("   -Psum is already built for Relation R.\n");
    }

    /* Create Relation S histogram and psum */
    int32_t **histogramS = createHistogram((*reIS));    // Allocation-errors are handled internally.

    if ((*reIS)->psum == NULL) {
        (*reIS)->psum = createPsum(histogramS);    // Allocation-errors are handled internally.
    } else {
        //printf("   -Psum is already built for Relation S.\n");
    }

    /* Allocate memory for the new relations R' and S'. */
    /* And partition the relations. */
    if ((*reIR)->is_partitioned == FALSE) {

        //printf("  -Phase 1: Partitioning the relation R.\n");

        (*reIR)->paritioned_relation = allocateRelation((*reIR)->num_tuples,
                                                        TRUE);    // Allocation-errors are handled internally.
        partition((*reIR), &(*reIR)->paritioned_relation, (*reIR)->psum);
        (*reIR)->is_partitioned = TRUE;

    } else {
        //printf("   -Relation R is already partitioned.\n");
    }

    if ((*reIS)->is_partitioned == FALSE) {

        //printf("  -Phase 1: Partitioning the relation S.\n");

        (*reIS)->paritioned_relation = allocateRelation((*reIS)->num_tuples,
                                                        TRUE);    // Allocation-errors are handled internally.
        partition((*reIS), &(*reIS)->paritioned_relation, (*reIS)->psum);
        (*reIS)->is_partitioned = TRUE;
    } else {
        //printf("   -Relation S is already partitioned.\n");
    }

#if PRINTING || DEEP_PRINTING
    printAllForPartition(4, (*reIR), (*reIS), histogramR, histogramS, (*reIR)->psum, (*reIR)->psum, (*reIR)->paritioned_relation, (*reIS)->paritioned_relation);
    printf("\n\n");
#endif

    /* The bucket and index array of one of the 2 relation already exists */

    if ((*reIR)->is_built == TRUE) {

        //printf("   -Relation R already has an index.\n");
        //printf("  -Phase 3: Joining the relations.\n");
        result = joinRelations((*reIR)->paritioned_relation, (*reIS)->paritioned_relation, (*reIR)->psum,
                               (*reIS)->psum, (*reIR)->bucket_index, (*reIR)->chain, TRUE);

    } else if ((*reIS)->is_built == TRUE) {

        //printf("   -Relation S already has an index.\n");
        //printf("  -Phase 3: Joining the relations.\n");
        result = joinRelations((*reIS)->paritioned_relation, (*reIR)->paritioned_relation, (*reIS)->psum,
                               (*reIR)->psum, (*reIS)->bucket_index, (*reIS)->chain, FALSE);
    }
        /* Build the bucket_index and the chain arrays of the smaller relation */
    else if ((*reIR)->num_tuples <= (*reIS)->num_tuples) {

        allocateAndInitializeBucketIndexAndChain(&(*reIR)->chain,
                                                 &(*reIR)->bucket_index);    // Allocation-errors are handled internally.

        //printf("  -Phase 2: Building index on the smaller relation R.\n");
        buildSmallestPartitionedRelationIndex((*reIR)->paritioned_relation, (*reIR)->psum, &(*reIR)->bucket_index,
                                              &(*reIR)->chain);    // Allocation-errors are handled internally.

        (*reIR)->is_built = TRUE;

        //printChainArray(psumR, relationNewR, chain);
        //printf("  -Phase 3: Joining the relations.\n");
        result = joinRelations((*reIR)->paritioned_relation, (*reIS)->paritioned_relation, (*reIR)->psum, (*reIS)->psum,
                               (*reIR)->bucket_index, (*reIR)->chain, TRUE);
    } else {

        allocateAndInitializeBucketIndexAndChain(&(*reIS)->chain, &(*reIS)->bucket_index);    // Allocation-errors are handled internally.

        //printf("  -Phase 2: Building index on the smaller relation S.\n");
        buildSmallestPartitionedRelationIndex((*reIS)->paritioned_relation, (*reIS)->psum, &(*reIS)->bucket_index,
                                              &(*reIS)->chain);    // Allocation-errors are handled internally.

        (*reIS)->is_built = TRUE;

        //printChainArray(psumS, relationNewS, chain);
        //printf("  -Phase 3: Joining the relations.\n");
        result = joinRelations((*reIS)->paritioned_relation, (*reIR)->paritioned_relation, (*reIS)->psum, (*reIR)->psum,
                               (*reIS)->bucket_index, (*reIS)->chain, FALSE);
    }


#if PRINTING || DEEP_PRINTING
    end_t = clock();
    total_t = (clock_t) ((double) (end_t - start_t) / CLOCKS_PER_SEC);
    printf("  -Total time taken by CPU for RadixHashJoin: %f seconds.\n", (double) total_t);
#endif


    /* If the relation is not a full column, then we are not going to need it anymore, so free it */
    if ((*reIR)->is_full_column == FALSE) {
        deAllocateRelation(&(*reIR));
    }

    if ((*reIS)->is_full_column == FALSE) {
        deAllocateRelation(&(*reIS));
    }


    /* Free Histograms */
    for (i = 0; i < number_of_buckets; i++) {
        free(histogramR[i]);
        free(histogramS[i]);
    }
    free(histogramR);
    free(histogramS);


    //printf(" -Join finished.\n");

    return result;
}

void thread_partition(Partition_struct **partition_struct) {

    int j;
    int indexOfNewR = (*partition_struct)->start;
    int currHashCounter = 0;

    for (j = 0; j < (*partition_struct)->relation->num_tuples; j++) {
        /*If we find the current bucket's key in relation-table, append the relation-table's data to the new relation-table. */
        if ((*partition_struct)->relation->tuples[j].payload % number_of_buckets == (*partition_struct)->hashValue) {
            (*partition_struct)->newRelation->tuples[indexOfNewR].key = (*partition_struct)->relation->tuples[j].key;
            (*partition_struct)->newRelation->tuples[indexOfNewR].payload = (*partition_struct)->relation->tuples[j].payload;
            indexOfNewR++;
            currHashCounter++;
            /* If all occurrences have been found, go to the next bucket.*/
            if (currHashCounter == (*partition_struct)->hashApperances)
                break;
        }
    }

    /* Increment the jobs_done var and signal the main process */
    pthread_mutex_lock(&threadpool->jobs_done_mtx);
    threadpool->jobs_done++;
    pthread_cond_signal(&threadpool->all_jobs_done);
    pthread_mutex_unlock(&threadpool->jobs_done_mtx);
}

/* Partition Relation */
void partition(Relation *relation, Relation **relationNew, int32_t **psum) {

    int i = 0, j = 0, jobs_added_to_queue = 0;
    unsigned int indexOfNewR = 0;
    int32_t currHashAppearances = 0, currHashCounter = 0;
    Partition_struct **partition_struct = NULL;

    /* Fill out the new Relation using the psum */
    for (i = 0; i < number_of_buckets; i++) {

        /* If this is the last bucket, find the number of occurrences */
        if (i == number_of_buckets - 1)
            currHashAppearances = relation->num_tuples - psum[i][1];
        else
            currHashAppearances = psum[i + 1][1] - psum[i][1];

        if (currHashAppearances == 0)
            continue;

        currHashCounter = 0;

        //printf("Bucket: %d, Psum start: %d, index of new R: %d\n", i, psum[i][1], indexOfNewR);

        /* Search occurrences of this bucket's key inside the relation table. */
        if (MULTITHREADING == FALSE/*relation->num_tuples < number_of_buckets * 10*/) {
            for (j = 0; j < relation->num_tuples; j++) {
                /*If we find the current bucket's key in relation-table, append the relation-table's data to the new relation-table. */
                if (relation->tuples[j].payload % number_of_buckets == psum[i][0]) {
                    (*relationNew)->tuples[indexOfNewR].key = relation->tuples[j].key;
                    (*relationNew)->tuples[indexOfNewR].payload = relation->tuples[j].payload;
                    indexOfNewR++;
                    currHashCounter++;
                    /* If all occurrences have been found, go to the next bucket.*/
                    if (currHashCounter == currHashAppearances)
                        break;
                }
            }
        } else {
            if (partition_struct == NULL) {
                partition_struct = myMalloc(sizeof(Partition_struct *) * number_of_buckets);
                for (j = 0; j < number_of_buckets; j++) {
                    partition_struct[j] = NULL;
                }
            }
            partition_struct[i] = myMalloc(sizeof(Partition_struct));
            partition_struct[i]->start = psum[i][1];
            partition_struct[i]->hashValue = psum[i][0];
            partition_struct[i]->hashApperances = currHashAppearances;
            partition_struct[i]->relation = relation;
            partition_struct[i]->newRelation = *relationNew;
            threadpool_add_job(threadpool, (void *) thread_partition, &partition_struct[i]);
            jobs_added_to_queue++;
        }
    }

    while (threadpool->jobs_done != jobs_added_to_queue) {
        pthread_cond_wait(&threadpool->all_jobs_done, &threadpool->jobs_done_mtx);
    }
    threadpool->jobs_done = 0;
    if (partition_struct != NULL) {
        for (j = 0; j < number_of_buckets; j++) {
            if (partition_struct[j] != NULL)
                free(partition_struct[j]);
        }
        free(partition_struct);
    }
}

void allocateAndInitializeBucketIndexAndChain(int ***chain, int ***bucket_index) {

    /* Create an array that contains bucket_index arrays of size H2_PARAM(for example 101) each */
    *bucket_index = myMalloc(sizeof(int *) * number_of_buckets);

    /* Create an array that contains the chain arrays */
    *chain = myMalloc(sizeof(int *) * number_of_buckets);

    /* Initialise pointers to NULL */
    for (int i = 0; i < number_of_buckets; i++) {
        (*bucket_index)[i] = NULL;
        (*chain)[i] = NULL;
    }
}

/* Create histogram of Relation */
void thread_histogram(Histogram_struct **histogram_struct) {
    for (int i = (*histogram_struct)->start; i < (*histogram_struct)->end; i++)
        (*histogram_struct)->histogram[(*histogram_struct)->relation->tuples[i].payload % number_of_buckets]++;

    /* Increment the jobs_done var and signal the main process */
    pthread_mutex_lock(&threadpool->jobs_done_mtx);
    threadpool->jobs_done++;
    pthread_cond_signal(&threadpool->all_jobs_done);
    pthread_mutex_unlock(&threadpool->jobs_done_mtx);
}

/* Create histogram of Relation */
int32_t **createHistogram(Relation *relation) {

    int i, j, chunk_size, bonus, start, end;

    /*Allocate memory for an Histogram-2dArray with size (number_of_buckets * 2) */
    int32_t **histogram = myMalloc(sizeof(int32_t *) * number_of_buckets);

    for (i = 0; i < number_of_buckets; i++) {
        histogram[i] = myMalloc(sizeof(int32_t) * 2);
        /* Histogram contains Hash key-Number of hash key appearances */
        histogram[i][0] = i;
        histogram[i][1] = 0;
    }

    /* If the number of threads is 8 for example,
     * then only use multithreading if number of tuples of the relation is more than 80(threshold)*/
    if (relation->num_tuples < number_of_buckets * 10 || MULTITHREADING == FALSE) {
        /* Fill out the histogram according to the hash values */
        for (i = 0; i < relation->num_tuples; i++) {
            histogram[relation->tuples[i].payload % number_of_buckets][1]++;
        }
    } else {
        Histogram_struct **histogram_struct = myMalloc(sizeof(Histogram_struct *) * number_of_buckets);
        chunk_size = relation->num_tuples / number_of_buckets;
        bonus = relation->num_tuples - chunk_size * number_of_buckets;  // i.e. remainder
        for (i = 0, start = 0, end = chunk_size;
             start < relation->num_tuples;
             start = end, end = start + chunk_size, i++) {

            if (bonus) {
                end++;
                bonus--;
            }
            //printf("la: Start: %d, End: %d, Bonus: %d, i: %d \n", start, end, bonus, i);

            /* Create a histogram struct to be passed to each thread */
            histogram_struct[i] = myMalloc(sizeof(Histogram_struct));
            histogram_struct[i]->histogram = myMalloc(sizeof(int32_t) * number_of_buckets);
            for (j = 0; j < number_of_buckets; j++)
                histogram_struct[i]->histogram[j] = 0;
            histogram_struct[i]->start = start;
            histogram_struct[i]->end = end;
            histogram_struct[i]->relation = relation;
            /* do something with array slice over [start, end) interval */
            threadpool_add_job(threadpool, (void *) thread_histogram, &histogram_struct[i]);
        }

        while (threadpool->jobs_done != number_of_buckets) {
            pthread_cond_wait(&threadpool->all_jobs_done, &threadpool->jobs_done_mtx);
        }

        for (i = 0; i < number_of_buckets; i++) {
            for (j = 0; j < number_of_buckets; j++) {
                histogram[i][1] += histogram_struct[j]->histogram[i];
            }
        }
        for (i = 0; i < number_of_buckets; i++) {
            free(histogram_struct[i]->histogram);
            free(histogram_struct[i]);
        }

        free(histogram_struct);

        threadpool->jobs_done = 0;
    }

    return histogram;
}

/* Create psum of Relation using its histogram */
int32_t **createPsum(int32_t **histogram) {

    int i;

    /*Allocate memory for Psum-2dArray with size (number_of_buckets * 2) */
    int32_t **psum = myMalloc(sizeof(int32_t *) * number_of_buckets);

    for (i = 0; i < number_of_buckets; i++) {

        psum[i] = myMalloc(sizeof(int32_t) * 2);
        /* Psum contains Hash key-Index of bucket */
        psum[i][0] = i;
        psum[i][1] = 0;
    }

    for (i = 1; i < number_of_buckets; i++)
        psum[i][1] = psum[i - 1][1] + histogram[i - 1][1];

    return psum;
}

/* Build the Index and the Chain Arrays of the relation with the less amount of tuples */
void buildSmallestPartitionedRelationIndex(Relation *rel, int32_t **psum, int32_t ***bucket_index, int32_t ***chain) {
    int i, j, num_tuples_of_currBucket;

    for (i = 0; i < number_of_buckets; i++) {

        /* Calculate the number of tuples in the i-th bucket using the Psum */
        if (i == number_of_buckets - 1)
            num_tuples_of_currBucket = rel->num_tuples - psum[i][1];
        else
            num_tuples_of_currBucket = psum[i + 1][1] - psum[i][1];

        /* Allocate memory for the ith-chain array, same size as the ith-bucket,
         * only if the number of tuples in the i-th bucket is greater than 0 */
        if (num_tuples_of_currBucket <= 0)
            continue;

        (*chain)[i] = myMalloc(sizeof(int) * num_tuples_of_currBucket);

        /* Also, create a bucket_index array only if the number of tuples in the i-th bucket are greater than 0,
         * to save memory */
        (*bucket_index)[i] = myMalloc(sizeof(int) * H2_PARAM);

        /* Initialise bucket_index array elements to 0*/
        for (j = 0; j < H2_PARAM; j++)
            (*bucket_index)[i][j] = 0;

        /* Fill chain and bucket_index arrays */
        for (int k = 0; k < num_tuples_of_currBucket; k++) {
            /* Iterate in R' */
            int index_A = psum[i][1] + k;
            int32_t h2 = rel->tuples[index_A].payload % H2_PARAM;

            if ((*bucket_index)[i][h2] == 0) {
                (*bucket_index)[i][h2] = k + 1;
                (*chain)[i][k] = 0;
            } else {
                (*chain)[i][k] = (*bucket_index)[i][h2];
                (*bucket_index)[i][h2] = k + 1;
            }
        }
    }
}

void thread_joinRelation(Join_struct **join_struct) {

    int j, i = (*join_struct)->current_bucket, currentIndex;
    int32_t h2Value;
    (*join_struct)->start_of_list = myMalloc(sizeof(Result));
    (*join_struct)->start_of_list->next_result = NULL;
    (*join_struct)->start_of_list->num_joined_rowIDs = 0;
    Result *current_result = (*join_struct)->start_of_list;

    /* If the num of tuples isn't 0, use the bucket_index to find same values of relNoIndex in relWithIndex */
    for (j = (*join_struct)->start; j < (*join_struct)->end; j++) {

        h2Value = (*join_struct)->relNoIndex->tuples[j].payload % H2_PARAM;
        currentIndex = (*join_struct)->bucket_index[i][h2Value];

        if (currentIndex == 0) {
            continue;
        }

        /* If the value isn't 0, then a tuple with the same h2 exists.
         * If it has the same value, then join-group both */

        do {
            if ((*join_struct)->relNoIndex->tuples[j].payload == (*join_struct)->relWithIndex->tuples[currentIndex - 1 + (*join_struct)->psumWithIndex[i][1]].payload) {

                /* If the current result is full, then create a new one and point to it with current result */
                if (current_result->num_joined_rowIDs == JOINED_ROWIDS_NUM) {
                    current_result->next_result = myMalloc(sizeof(Result));
                    current_result->next_result->num_joined_rowIDs = 0;
                    current_result = current_result->next_result;
                    current_result->next_result = NULL;
                }
                /* We always want to write R's rowIDs first and S' rowIDs second. */
                /* So we use the variable "is_R_relation_first" to determine which relation was sent first. */
                if ((*join_struct)->is_R_relation_first) {
                    /* If the current result isn' t full, insert a new rowid combo (rowIDR, rowIDS)
                     * and increment the num_joined_rowIDs variable */
                    current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = (*join_struct)->relWithIndex->tuples[currentIndex - 1 +
                                                                                                                               (*join_struct)->psumWithIndex[i][1]].key;
                    current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = (*join_struct)->relNoIndex->tuples[j].key;
                } else {
                    current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = (*join_struct)->relNoIndex->tuples[j].key;
                    current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = (*join_struct)->relWithIndex->tuples[currentIndex - 1 +
                                                                                                                               (*join_struct)->psumWithIndex[i][1]].key;
                }
                current_result->num_joined_rowIDs++;
            } else {
            }
            /* If a chain exists, meaning there is another tuple of relWithIndex with the same h2, check it too*/
            currentIndex = (*join_struct)->chain[i][currentIndex - 1];
        } while (currentIndex != 0);
    }

    (*join_struct)->end_of_list = current_result;

    /* Increment the jobs_done var and signal the main process */
    pthread_mutex_lock(&threadpool->jobs_done_mtx);
    threadpool->jobs_done++;
    pthread_cond_signal(&threadpool->all_jobs_done);
    pthread_mutex_unlock(&threadpool->jobs_done_mtx);
}

/* Join two relations and return the result.
 * RelWithIndex: The index of this relation will be used for the comparison and the join.
 * RelNoIndex: This relation might or might not have index and we will use the index of the other relation to compare and join.
 * */
Result *joinRelations(Relation *relWithIndex, Relation *relNoIndex, int32_t **psumWithIndex, int32_t **psumNoIndex,
                      int32_t **bucket_index, int32_t **chain,
                      bool is_R_relation_first) {

    int i, j, k, num_tuples_of_currBucket, currentIndex, jobs_added_to_queue = 0, pointer_to_start_of_list = FALSE;
    int32_t h2Value;
    Join_struct **join_struct = NULL;

    Result *result, *current_result, *pointer_to_end_of_result;
    result = myMalloc(sizeof(Result));
    current_result = result;
    pointer_to_end_of_result = NULL;
    result->next_result = NULL;
    result->num_joined_rowIDs = 0;

    /* For each bucket of relNoIndex, we check if there is such bucket in relWithIndex and if so, we continue by checking the values inside. */
    for (i = 0; i < number_of_buckets; i++) {

        //printf("\n---------------------- Bucket: %d -----------------------\n", i);

        /* Check if the i-th bucket of relWithIndex is empty */
        /* If it is, then go to the next bucket */
        if (chain[i] == NULL) {
            //printf("Bucket #%d in \"Relation with Index\" is empty, continuing to the next bucket!\n", i);
            continue;
        }

        /* If i-th bucket of relWithIndex isn't empty, calculate the num of tuples in the i-th bucket of relNoIndex */
        if (i == number_of_buckets - 1)
            num_tuples_of_currBucket = relNoIndex->num_tuples - psumNoIndex[i][1];
        else
            num_tuples_of_currBucket = psumNoIndex[i + 1][1] - psumNoIndex[i][1];

        /* If the num of tuples in the i-th bucket of relNoIndex is 0, go to the next bucket */
        if (num_tuples_of_currBucket == 0) {
            //printf("Bucket #%d in \"Relation without Index\" is empty, continuing to the next bucket!\n", i);
            continue;   // Next bucket in relNoIndex
        }

        if (!MULTITHREADING) {
            /* If the num of tuples isn't 0, use the bucket_index to find same values of relNoIndex in relWithIndex */
            for (j = psumNoIndex[i][1]; j < psumNoIndex[i][1] + num_tuples_of_currBucket; j++) {
                //printf("Checking(%d):\n", relNoIndex->tuples[j].payload);
                h2Value = relNoIndex->tuples[j].payload % H2_PARAM;
                currentIndex = bucket_index[i][h2Value];

                /* If the value of bucket_index[h2] is 0, it means there is no tuple with that h2, so go to the next tuple */
                if (currentIndex == 0) {
                    //printf("\th2(%d)=%d ~> b_i(%d)=%d ~> ", relNoIndex->tuples[j].payload, h2Value, h2Value, currentIndex);
                    //printf("No_Indexed_Tuple_With_H2=%d ~> NOT_JOIN \n", h2Value);
                    continue;
                }

                /* If the value isn't 0, then a tuple with the same h2 exists.
                 * If it has the same value, then join-group both */

                do {
                    //printf("\th2(%d)=%d ~> b_i(%d)=%d ~> ", relNoIndex->tuples[j].payload, h2Value, h2Value, currentIndex);
                    //printf("EQUAL(%d,%d)", relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].payload, relNoIndex->tuples[j].payload);
                    if (relNoIndex->tuples[j].payload == relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].payload) {

                        /* If the current result is full, then create a new one and point to it with current result */
                        if (current_result->num_joined_rowIDs == JOINED_ROWIDS_NUM) {
                            current_result->next_result = myMalloc(sizeof(Result));
                            current_result->next_result->num_joined_rowIDs = 0;
                            current_result = current_result->next_result;
                            current_result->next_result = NULL;
                        }
                        /* We always want to write R's rowIDs first and S' rowIDs second. */
                        /* So we use the variable "is_R_relation_first" to determine which relation was sent first. */
                        if (is_R_relation_first) {
                            //printf("=TRUE ~> JOIN[%d|%d]\n", relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].key, relNoIndex->tuples[j].key);
                            /* If the current result isn' t full, insert a new rowid combo (rowIDR, rowIDS)
                             * and increment the num_joined_rowIDs variable */
                            current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].key;
                            current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = relNoIndex->tuples[j].key;
                        } else {
                            //printf("=TRUE ~> JOIN[%d|%d]\n", relNoIndex->tuples[j].key, relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].key);
                            current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = relNoIndex->tuples[j].key;
                            current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].key;
                        }
                        current_result->num_joined_rowIDs++;
                    } else {
                        //printf("=FALSE ~> NOT_JOIN\n");
                    }
                    /* If a chain exists, meaning there is another tuple of relWithIndex with the same h2, check it too*/
                    currentIndex = chain[i][currentIndex - 1];
                } while (currentIndex != 0);
            }
        } else {
            if (join_struct == NULL) {
                join_struct = myMalloc(sizeof(Join_struct *) * number_of_buckets);
                for (k = 0; k < number_of_buckets; k++) {
                    join_struct[k] = NULL;
                }
            }
            join_struct[i] = myMalloc(sizeof(Join_struct));
            join_struct[i]->chain = chain;
            join_struct[i]->bucket_index = bucket_index;
            join_struct[i]->psumNoIndex = psumNoIndex;
            join_struct[i]->psumWithIndex = psumWithIndex;
            join_struct[i]->relNoIndex = relNoIndex;
            join_struct[i]->relWithIndex = relWithIndex;
            join_struct[i]->is_R_relation_first = is_R_relation_first;
            join_struct[i]->start = psumNoIndex[i][1];
            join_struct[i]->end = psumNoIndex[i][1] + num_tuples_of_currBucket;
            join_struct[i]->start_of_list = NULL;
            join_struct[i]->end_of_list = NULL;
            join_struct[i]->current_bucket = i;

            threadpool_add_job(threadpool, (void *) thread_joinRelation, &join_struct[i]);
            jobs_added_to_queue++;

        }
    }

    while (threadpool->jobs_done != jobs_added_to_queue) {
        pthread_cond_wait(&threadpool->all_jobs_done, &threadpool->jobs_done_mtx);
    }

    for (i = 0; i < number_of_buckets; i++) {
        if (join_struct == NULL)
            break;
        else {
            if (join_struct[i] != NULL) {
                if (pointer_to_start_of_list == FALSE) {
                    free(result);
                    result = join_struct[i]->start_of_list;
                    pointer_to_end_of_result = join_struct[i]->end_of_list;
                    pointer_to_start_of_list = TRUE;
                    continue;
                }
                pointer_to_end_of_result->next_result = join_struct[i]->start_of_list;
                pointer_to_end_of_result = join_struct[i]->end_of_list;
            }
        }
    }

    threadpool->jobs_done = 0;
    if (join_struct != NULL) {
        for (j = 0; j < number_of_buckets; j++) {
            free(join_struct[j]);
        }
        free(join_struct);
    }

    return result;
}

/* De-allocate Relation memory */
void deAllocateRelation(Relation **relation) {

    free((*relation)->tuples);

    for (int i = 0; i < number_of_buckets; i++) {

        if ((*relation)->psum != NULL)
            free((*relation)->psum[i]);

        if ((*relation)->chain != NULL)
            free((*relation)->chain[i]);

        if ((*relation)->bucket_index != NULL)
            free((*relation)->bucket_index[i]);
    }

    if ((*relation)->psum != NULL)
        free((*relation)->psum);

    if ((*relation)->chain != NULL)
        free((*relation)->chain);

    if ((*relation)->bucket_index != NULL)
        free((*relation)->bucket_index);

    if ((*relation)->paritioned_relation != NULL) {
        free((*relation)->paritioned_relation->tuples);
        free((*relation)->paritioned_relation);
    }

    free((*relation));
    *relation = NULL;
}

void deAllocateResult(Result **result) {

    Result *next_result;

    do {
        next_result = (*result)->next_result;
        free((*result));
        (*result) = next_result;
    } while ((*result) != NULL);
}
