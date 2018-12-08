#ifndef ADVANCED_DBMS_IMPLEMENTATION_RADIXHASHJOIN_H
#define ADVANCED_DBMS_IMPLEMENTATION_RADIXHASHJOIN_H

#include "query_functions.h"

int testRHJ();

/* Radix Hash Join */
Result *RadixHashJoin(Relation **reIR, Relation **reIS, int number_of_buckets);

/* Partition Relation */
void partition(Relation *relation, Relation **relationNew, int number_of_buckets, int32_t **psum);

/* Create histogram of Relation */
int32_t **createHistogram(Relation *relation, int number_of_buckets);

/* Create psum of Relation using its histogram */
int32_t **createPsum(int number_of_buckets, int32_t **histogram);

void allocateAndInitializeBucketIndexAndChain(int ***chain, int ***bucket_index, int number_of_buckets);

/* Build the Index and the Chain Arrays of the relation with the less amount of tuples */
void buildSmallestPartitionedRelationIndex(Relation *rel, int32_t **psum, int32_t ***bucket_index, int32_t ***chain, int number_of_buckets);

/* Join two relations and return the result. */
Result *joinRelations(Relation *relWithIndex, Relation *relNoIndex, int32_t **psumWithIndex, int32_t **psumNoIndex,
                      int32_t **bucket_index, int32_t **chain, int number_of_buckets, bool is_R_relation_first);

/* De-allocate Relation memory */
void deAllocateRelation(Relation **relation, int number_of_buckets);

/* De-allocate Result memory */
void deAllocateResult(Result **result);


#endif //ADVANCED_DBMS_IMPLEMENTATION_RADIXHASHJOIN_H
