#include "radixHashJoin.h"



int testRHJ()
{
    // n  = cache size / maxsizeofbucket;

    int cache_size = 6 * (1024 * 1024); // Cache size is 6mb
    int i, j;
    /* H1_PARAM is the number of the last-n bits of the 32-bit number we wanna keep */
    int32_t n = H1_PARAM;

    /* So we are going to use mod(%2^n) to get the last n bits, where 2^n is also the number of buckets */
    int32_t number_of_buckets = (int32_t) pow(2, n);

    Relation **relations = malloc(sizeof(Relation *) * 10);
    uint32_t relation_size[10] = {20, 10, 10, 10, 10, 10, 10, 10, 10, 10};


    /* Allocate the relations and initialize them with random numbers from 0-200 */
    for (i = 0; i < 10; i++) {
        if ((relations[i] = allocateRelation(relation_size[i])) == NULL) {
            return EXIT_FAILURE;  // ErrorCode
        }
        initializeRelationWithRandomNumbers(&relations[i], number_of_buckets);
    }

    /* Do Radix Hash Join on the conjunction of the relations */
    Result *result = RadixHashJoin(relations[0], relations[1], number_of_buckets);
    if (result == NULL) {
        printf("Malloc failed!\n");
        perror("Malloc");
        return EXIT_FAILURE;
    }


#if PRINTING
    printResults(result);
#endif



    /*De-allocate memory*/

    for (i = 0; i < 10; i++) {
        free(relations[i]->tuples);
        free(relations[i]);
    }
    free(relations);

    do {
        Result *next_result = result->next_result;
        free(result);
        result = next_result;
    } while (result != NULL);

}


/* Relation R: reIR, Relation S: reIS, Number of buckets: 2^n */
Result *RadixHashJoin(Relation *reIR, Relation *reIS, int number_of_buckets) {

    clock_t start_t, end_t, total_t;

    printf("\n# Running RadixHashJoin on Relations S and R.\n");
    start_t = clock();

/*    reIR->tuples[reIR->num_tuples - 2].payload = 929;
    reIR->tuples[reIR->num_tuples - 2].key = reIR->tuples[reIR->num_tuples - 2].payload % number_of_buckets;

    reIR->tuples[reIR->num_tuples - 3].payload = 828;
    reIR->tuples[reIR->num_tuples - 3].key = reIR->tuples[reIR->num_tuples - 3].payload % number_of_buckets;

    reIS->tuples[reIS->num_tuples - 1].payload = 929;
    reIS->tuples[reIS->num_tuples - 1].key = reIS->tuples[reIS->num_tuples - 1].payload % number_of_buckets;

    reIS->tuples[reIS->num_tuples - 2].payload = 20;
    reIS->tuples[reIS->num_tuples - 2].key = reIS->tuples[reIS->num_tuples - 2].payload % number_of_buckets;*/


    /* Construct Histograms an Psums */

    /* Create Relation R histogram and psum */
    int32_t **histogramR = createHistogram(reIR, number_of_buckets);
    if (histogramR == NULL) {
        return NULL;
    }
    int32_t **psumR = createPsum(number_of_buckets, histogramR);
    if (psumR == NULL) {
        return NULL;
    }
    /* Create Relation S histogram and psum */
    int32_t **histogramS = createHistogram(reIS, number_of_buckets);
    if (histogramS == NULL) {
        return NULL;
    }
    int32_t **psumS = createPsum(number_of_buckets, histogramS);
    if (psumS == NULL) {
        return NULL;
    }

    /* Create new relations to assign the partitioned relations. */
    Relation *relationNewR, *relationNewS;

    /* Allocate memory for new matrices R' and S' that will be used as hash tables. */
    if ((relationNewR = allocateRelation(reIR->num_tuples)) == NULL) {
        return NULL;
    }
    if ((relationNewS = allocateRelation(reIS->num_tuples)) == NULL) {
        return NULL;
    }

    /* Partition the relations */
    partition(reIR, &relationNewR, number_of_buckets, psumR);
    partition(reIS, &relationNewS, number_of_buckets, psumS);
    printf("  -Phase 1: Partitioning the relations.\n");

#if PRINTING
    printAllForPartition(4, reIR, reIS, histogramR, histogramS, psumR, psumS, relationNewR, relationNewS, number_of_buckets);
    printf("\n\n");
#endif

    /* Bucket-Chain */
    int **chain;
    int **bucket_index;
    if (allocateAndInitializeBucketIndexAndChain(&chain, &bucket_index, number_of_buckets) == -1)
        return NULL;

    Result *result;

    /* Build the bucket_index and the chain arrays of the smaller relation */
    if (relationNewR->num_tuples <= relationNewS->num_tuples) {
        printf("  -Phase 2: Building index on the smaller relation.\n");
        if (buildSmallestPartitionedRelationIndex(relationNewR, psumR, &bucket_index, &chain, number_of_buckets) == -1)
            return NULL;

        //printChainArray(number_of_buckets, psumR, relationNewR, chain);
        printf("  -Phase 3: Joining the relations.\n");
        if ((result = joinRelations(relationNewR, relationNewS, psumR, psumS, bucket_index, chain, number_of_buckets,
                                    TRUE)) == NULL)
            return NULL;
    } else {
        printf("  -Phase 2: Building index on the smaller relation.\n");
        if (buildSmallestPartitionedRelationIndex(relationNewS, psumS, &bucket_index, &chain, number_of_buckets) == -1)
            return NULL;
        //printChainArray(number_of_buckets, psumS, relationNewS, chain);
        printf("  -Phase 3: Joining the relations.\n");
        if ((result = joinRelations(relationNewS, relationNewR, psumS, psumR, bucket_index, chain, number_of_buckets,
                                    FALSE)) == NULL)
            return NULL;
    }

    deAllocateRadixHashJoinMemory(histogramR, histogramS, psumR, psumS, chain, bucket_index, relationNewR, relationNewS,
                                  number_of_buckets);

    end_t = clock();

    total_t = (clock_t)((double) (end_t - start_t) / CLOCKS_PER_SEC);
#if PRINTING
    printf("  -Total time taken by CPU for RadixHashJoin: %f seconds.\n", (double) total_t);
#endif

    printf(" -Join finished.\n");

    return result;
}

/* Partition Relation */
void partition(Relation *relation, Relation **relationNew, int number_of_buckets, int32_t **psum) {

    int i, j;
    unsigned int indexOfNewR = 0;
    int32_t currHashAppearances = 0, currHashCounter = 0;

    /* Fill out the new Relation using the psum */
    for (i = 0; i < number_of_buckets; i++) {

        currHashCounter = 0;

        /* If this is the last bucket, find the number of occurrences*/
        if (i == number_of_buckets - 1)
            currHashAppearances = relation->num_tuples - psum[i][1];
        else
            currHashAppearances = psum[i + 1][1] - psum[i][1];

        if (currHashAppearances == 0)
            continue;

        /* Search occurrences of this bucket's key inside the relation table. */
        for (j = 0; j < relation->num_tuples; j++) {

            /* If all occurrences have been found, go to the next bucket.*/
            if (currHashCounter == currHashAppearances)
                break;

            /*If we find the current bucket's key in relation-table, append the relation-table's data to the new relation-table. */
            if (relation->tuples[j].key == psum[i][0]) {
                (*relationNew)->tuples[indexOfNewR].key = relation->tuples[j].key;
                (*relationNew)->tuples[indexOfNewR].payload = relation->tuples[j].payload;
                (*relationNew)->tuples[indexOfNewR].rowID = relation->tuples[j].rowID;
                indexOfNewR++;
                currHashCounter++;
            }
        }
    }
}

int allocateAndInitializeBucketIndexAndChain(int ***chain, int ***bucket_index, int number_of_buckets) {

    /* Create an array that contains bucket_index arrays of size H2_PARAM(for example 101) each */
    *bucket_index = malloc(sizeof(int *) * number_of_buckets);
    if ((*bucket_index) == NULL) {
        printf("Malloc failed!\n");
        perror("Malloc");
        return -1;
    }

    /* Create an array that contains the chain arrays */
    *chain = malloc(sizeof(int *) * number_of_buckets);
    if ((*chain) == NULL) {
        printf("Malloc failed!\n");
        perror("Malloc");
        return -1;
    }

    /* Initialise pointers to NULL */
    for (int i = 0; i < number_of_buckets; i++) {
        (*bucket_index)[i] = NULL;
        (*chain)[i] = NULL;
    }

    return 0;
}

/* Create histogram of Relation */
int32_t **createHistogram(Relation *relation, int number_of_buckets) {

    int i;

    /*Allocate memory for an Histogram-2dArray with size (number_of_buckets * 2) */
    int32_t **histogram = malloc(sizeof(int32_t * ) * number_of_buckets);
    if (histogram == NULL) {
        printf("Malloc failed!\n");
        perror("Malloc");
        return NULL;
    }


    for (i = 0; i < number_of_buckets; i++) {
        histogram[i] = malloc(sizeof(int32_t) * 2);
        if (histogram[i] == NULL) {
            printf("Malloc failed!\n");
            perror("Malloc");
            return NULL;
        }

        /* Histogram contains Hash key-Number of hash key appearances */
        histogram[i][0] = i;
        histogram[i][1] = 0;
    }

    /* Fill out the histogram according to the hash values*/
    for (i = 0; i < relation->num_tuples; i++) {
        histogram[relation->tuples[i].key][1]++;
    }

    return histogram;
}

/* Create psum of Relation using its histogram */
int32_t **createPsum(int number_of_buckets, int32_t **histogram) {

    int i;

    /*Allocate memory for Psum-2dArray with size (number_of_buckets * 2) */
    int32_t **psum = malloc(sizeof(int32_t * ) * number_of_buckets);
    if (psum == NULL) {
        printf("Malloc failed!\n");
        perror("Malloc");
        return NULL;
    }

    for (i = 0; i < number_of_buckets; i++) {

        psum[i] = malloc(sizeof(int32_t) * 2);
        if (psum[i] == NULL) {
            printf("Malloc failed!\n");
            perror("Malloc");
            return NULL;
        }

        /* Psum contains Hash key-Index of bucket */
        psum[i][0] = i;
        psum[i][1] = 0;
    }

    for (i = 0; i < number_of_buckets; i++) {
        if (i == 0) {
            continue;
        } else {
            psum[i][1] = psum[i - 1][1] + histogram[i - 1][1];
        }
    }

    return psum;
}

/* Build the Index and the Chain Arrays of the relation with the less amount of tuples */
int buildSmallestPartitionedRelationIndex(Relation *rel, int32_t **psum, int32_t ***bucket_index, int32_t ***chain,
                                          int number_of_buckets) {
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

        (*chain)[i] = malloc(sizeof(int) * num_tuples_of_currBucket);
        if ((*chain)[i] == NULL) {
            printf("Malloc failed!\n");
            perror("Malloc");
            return -1;
        }

        /* Also, create a bucket_index array only if the number of tuples in the i-th bucket are greater than 0,
         * to save memory */
        (*bucket_index)[i] = malloc(sizeof(int) * H2_PARAM);
        if ((*bucket_index)[i] == NULL) {
            printf("Malloc failed!\n");
            perror("Malloc");
            return -1;
        }

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

    return 0;
}

/* Join two relations and return the result.
 * RelWithIndex: The index of this relation will be used for the comparison and the join.
 * RelNoIndex: This relation might or might not have index and we will use the index of the other relation to compare and join.
 * */
Result *joinRelations(Relation *relWithIndex, Relation *relNoIndex, int32_t **psumWithIndex, int32_t **psumNoIndex,
                      int32_t **bucket_index, int32_t **chain, int number_of_buckets, bool is_R_relation_first) {

    int i, j, num_tuples_of_currBucket, currentIndex;
    int32_t h2Value;

    Result *result = malloc(sizeof(Result));
    if (result == NULL) {
        return NULL;
    }
    Result *current_result = result;
    result->next_result = NULL;
    result->num_joined_rowIDs = 0;

    /* For each bucket of relNoIndex, we check if there is such bucket in relWithIndex and if so, we continue by checking the values inside. */
    for (i = 0; i < number_of_buckets; i++) {
#if PRINTING
        printf("\n---------------------- Bucket: %d -----------------------\n", i);
#endif
        /* Check if the i-th bucket of relWithIndex is empty */
        /* If it is, then go to the next bucket */
        if (chain[i] == NULL) {
#if PRINTING
            printf("Bucket #%d in \"Relation with Index\" is empty, continuing to the next bucket!\n", i);
#endif
            continue;
        }

        /* If i-th bucket of relWithIndex isn't empty, calculate the num of tuples in the i-th bucket of relNoIndex */
        if (i == number_of_buckets - 1)
            num_tuples_of_currBucket = relNoIndex->num_tuples - psumNoIndex[i][1];
        else
            num_tuples_of_currBucket = psumNoIndex[i + 1][1] - psumNoIndex[i][1];

        /* If the num of tuples in the i-th bucket of relNoIndex is 0, go to the next bucket */
        if (num_tuples_of_currBucket == 0) {
#if PRINTING
            printf("Bucket #%d in \"Relation without Index\" is empty, continuing to the next bucket!\n", i);
#endif
            continue;   // Next bucket in relNoIndex
        }

        /* If the num of tuples isn't 0, use the bucket_index to find same values of relNoIndex in relWithIndex */
        for (j = psumNoIndex[i][1]; j < psumNoIndex[i][1] + num_tuples_of_currBucket; j++) {
#if PRINTING
            printf("Checking(%d):\n", relNoIndex->tuples[j].payload);
#endif
            h2Value = relNoIndex->tuples[j].payload % H2_PARAM;
            currentIndex = bucket_index[i][h2Value];

            /* If the value of bucket_index[h2] is 0, it means there is no tuple with that h2, so go to the next tuple */
            if (currentIndex == 0) {
#if PRINTING
                printf("\th2(%d)=%d ~> b_i(%d)=%d ~> ", relNoIndex->tuples[j].payload, h2Value, h2Value,
                   currentIndex);
                printf("No_Indexed_Tuple_With_H2=%d ~> NOT_JOIN \n", h2Value);

#endif
                continue;
            }

            /* If the value isn't 0, then a tuple with the same h2 exists.
             * If it has the same value, then join-group both */
            do {
#if PRINTING
                printf("\th2(%d)=%d ~> b_i(%d)=%d ~> ", relNoIndex->tuples[j].payload, h2Value, h2Value,
                   currentIndex);
                printf("EQUAL(%d,%d)", relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].payload, relNoIndex->tuples[j].payload);
#endif
                if (relNoIndex->tuples[j].payload ==
                    relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].payload) {

                    /* If the current result is full, then create a new one and point to it with current result*/
                    if (current_result->num_joined_rowIDs == JOINED_ROWIDS_NUM) {
                        current_result->next_result = malloc(sizeof(Result));
                        current_result->next_result->num_joined_rowIDs = 0;
                        current_result = current_result->next_result;
                        current_result->next_result = NULL;
                    }
                    /* We always want to write R's rowIDs first and S' rowIDs second. */
                    /* So we use the variable "is_R_relation_first" to determine which relation was sent first. */
                    if (is_R_relation_first) {
#if PRINTING
                        printf("=TRUE ~> JOIN[%d|%d]\n", relWithIndex->tuples[
                                currentIndex - 1 + psumWithIndex[i][1]].rowID, relNoIndex->tuples[j].rowID);
#endif
                        /* If the current result isn' t full, insert a new rowid combo (rowIDR, rowIDS)
                         * and increment the num_joined_rowIDs variable */
                        current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = relWithIndex->tuples[
                                currentIndex - 1 + psumWithIndex[i][1]].rowID;
                        current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = relNoIndex->tuples[j].rowID;
                    } else {
#if PRINTING
                        printf("=TRUE ~> JOIN[%d|%d]\n", relNoIndex->tuples[j].rowID,
                                relWithIndex->tuples[currentIndex - 1 + psumWithIndex[i][1]].rowID);
#endif
                        current_result->joined_rowIDs[current_result->num_joined_rowIDs][0] = relNoIndex->tuples[j].rowID;
                        current_result->joined_rowIDs[current_result->num_joined_rowIDs][1] = relWithIndex->tuples[
                                currentIndex - 1 + psumWithIndex[i][1]].rowID;
                    }
                    current_result->num_joined_rowIDs++;
                } else {
#if PRINTING
                    printf("=FALSE ~> NOT_JOIN\n");
#endif
                }
                /* If a chain exists, meaning there is another tuple of relWithIndex with the same h2, check it too*/
                currentIndex = chain[i][currentIndex - 1];
            } while (currentIndex != 0);
        }
    }
    return result;
}

/* De-allocate memory */
void deAllocateRadixHashJoinMemory(int32_t **histogramR, int32_t **histogramS, int32_t **psumR, int32_t **psumS,
                                   int32_t **chain, int32_t **bucket_index, Relation *relationNewR,
                                   Relation *relationNewS, int number_of_buckets) {
    for (int i = 0; i < number_of_buckets; i++) {
        free(histogramR[i]);
        free(histogramS[i]);
        free(psumR[i]);
        free(psumS[i]);
        if (chain[i] != NULL)
            free(chain[i]);
        if (bucket_index[i] != NULL)
            free(bucket_index[i]);
    }

    free(chain);
    free(bucket_index);

    free(histogramR);
    free(psumR);

    free(histogramS);
    free(psumS);

    free(relationNewR->tuples);
    free(relationNewS->tuples);

    free(relationNewR);
    free(relationNewS);
}

void execute_query(Query_Info * query_info){
    print_query(query_info);
    free_query(query_info);
}

void print_query(Query_Info * query_info){


    int i;

    printf("\tSELECT ");
    if (query_info->selections != NULL) {
        for (i = 0; i < query_info->selection_count; i++) {
            if (query_info->selections[i] != NULL)
                printf("SUM(\"%d\".c%d)", query_info->selections[i][0], query_info->selections[i][1]);
            if (i != query_info->selection_count - 1)
                printf(", ");
        }
    }

    printf("\n\tFROM ");
    for (i = 0; i < query_info->relationId_count; i++) {
        printf("r%d \"%d\"", query_info->relation_IDs[i], i);
        if (i != query_info->relationId_count - 1)
            printf(", ");
    }

    printf("\n\tWHERE ");

    if (query_info->joins != NULL) {
        for (i = 0; i < query_info->join_count; i++) {
            if (query_info->joins[i] != NULL)
                printf("%d.c%d=%d.c%d", query_info->joins[i][0], query_info->joins[i][1], query_info->joins[i][2], query_info->joins[i][3]);
            if (query_info->filter_count != 0)
                printf(" and ");
        }
    }

    if (query_info->filters != NULL) {
        for (i = 0; i < query_info->filter_count; i++) {
            if (query_info->filters[i] != NULL)
                printf("%d.c%d(%d)%d", query_info->filters[i][0], query_info->filters[i][1], query_info->filters[i][2], query_info->filters[i][3]);
            if (i != query_info->filter_count - 1)
                printf(" and ");
        }
    }

}

void free_query(Query_Info * query_info){

    int i;

    free(query_info->relation_IDs);

    if (query_info->joins != NULL) {
        for (i = 0; i < query_info->join_count; i++) {
            if (query_info->joins[i] != NULL)
                free(query_info->joins[i]);
        }
        free(query_info->joins);
    }

    if (query_info->filters != NULL) {
        for (i = 0; i < query_info->filter_count; i++) {
            if (query_info->filters[i] != NULL)
                free(query_info->filters[i]);
        }
        free(query_info->filters);
    }

    if (query_info->selections != NULL) {
        for (i = 0; i < query_info->selection_count; i++) {
            if (query_info->selections[i] != NULL)
                free(query_info->selections[i]);
        }
        free(query_info->selections);
    }

    free(query_info);
}
