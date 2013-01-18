/******************************************************************************
 * Copyright (c) 2007-2013. F. Suter, S. Hunold.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL 2.1) which comes with this package.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#include "daggen_commons.h"

/*************************/
/** Static declarations **/
/*************************/
static DAG generateDAG(void);
static void generateTasks(DAG dag);
static void generateDependencies(DAG dag);
static void generateTransfers(DAG dag);
static void freeDAG(DAG dag);

/************/
/** main() **/
/************/

int main(int argc, char **argv)
{
  time_t now=time(NULL);
  DAG dag;
  int i;

  srand((unsigned int)getpid() + (unsigned int)time(NULL));
  /* parse command line options */
  if (parseOptions(argc, argv) == -1) {
    printUsage();
    exit(1);
  }


  /* putting header */
  fprintf(OUTPUT, "// DAG automatically generated by daggen at %s// ",
      ctime(&now));
  for (i=0;i<argc;i++) {
    fprintf(OUTPUT,"%s ",argv[i]);
  }
  fprintf(OUTPUT,"\n");

  /* generating the dag */
  dag=generateDAG();

  /* generate output */
  if (dag) {
    if (global.dot_output)
      outputDOT(dag);
    else
      outputDAG(dag);
  }

  /* free all created data structures */
  freeDAG(dag);

  if (OUTPUT != stdout)
    fclose(OUTPUT);

  exit(0);
}

/********************/
/** DAG generation **/
/********************/

static DAG generateDAG(void)
{
  DAG dag;

  dag = (DAG)calloc(1,sizeof(struct _DAG));

  /* Generating all the tasks */
  generateTasks(dag);

  /* Generating the Dependencies */
  generateDependencies(dag);

  /* Generating the transfer costs */
  generateTransfers(dag);

  return dag;
}

/*
 * generateTransfers()
 *
 * Enforces the CCR
 */
static void generateTransfers(DAG dag) {
  int i, j, k;

  /* assign costs.
	 Get the data size handled by the parent
	 Compute its square (matrix #elements)
	 multiply by 8 (double -> bytes)
	 Costs are in bytes
   */

  for (i=0; i<dag->nb_levels-1; i++) {
    for (j=0; j<dag->nb_tasks_per_level[i]; j++) {
      for (k=0; k<dag->levels[i][j]->nb_children; k++) {
        dag->levels[i][j]->comm_costs[k] = (pow(
            dag->levels[i][j]->data_size, 2.0)*8);
      }
    }
  }

  return;
}

/*
 * generateDependencies()
 */
static void generateDependencies(DAG dag) {
  int i, j, k, l, m, parent_index, parent_level;
  int nb_parents;
  Task parent;

  /* for all levels but the last one */
  /* operate at the "parent" level   */
  for (i=1; i<dag->nb_levels; i++) {
    for (j=0; j<dag->nb_tasks_per_level[i]; j++) {
      /* compute how many parent the task should have,
       * at least one of course */
      nb_parents = MIN(1 + (int)getRandomNumberBetween(0.0,
          global.density * (dag->nb_tasks_per_level[i-1])),
          dag->nb_tasks_per_level[i-1]);
      for (k=0; k<nb_parents; k++) {
        /* compute the level of the parent */
        parent_level = (i-(int)getRandomNumberBetween( 1.0,
            (double)global.jump+1));
        parent_level = MAX(0,parent_level);
        /* compute which parent */
        parent_index = (int)getRandomNumberBetween(0.0,
            (double)(dag->nb_tasks_per_level[parent_level]));

        parent = dag->levels[parent_level][parent_index];

        /* increment the parent_index until a slot is found */
        for (m=0; m<dag->nb_tasks_per_level[parent_level]; m++) {
          for (l=0; l<parent->nb_children; l++) {
            if (parent->children[l] == dag->levels[i][j]) {
              parent_index = (parent_index +1) %
                  (dag->nb_tasks_per_level[parent_level]);
              parent = dag->levels[parent_level][parent_index];
              break;
            }
          }
          if (l == parent->nb_children)
            break;
        }
        if (m < dag->nb_tasks_per_level[parent_level]) {
          /* update the parent's children list*/
          parent->children = (Task *)realloc(parent->children,
              (parent->nb_children+1)*sizeof(Task));
          parent->children[(parent->nb_children)] = dag->levels[i][j];
          (parent->nb_children)++;
        } else {
          /* giving up n that one, too hard to figure it out */
        }

      }
    }
  }

  /* Allocate memory for comm_costs and tags */
  for (i=0; i<dag->nb_levels; i++) {
    for (j=0; j<dag->nb_tasks_per_level[i]; j++) {
      dag->levels[i][j]->comm_costs = (double *)calloc(
          dag->levels[i][j]->nb_children, sizeof(double));
      dag->levels[i][j]->transfer_tags = (int *)calloc(
          dag->levels[i][j]->nb_children, sizeof(int));
    }
  }
}

/*
 * generateTasks()
 */
static void generateTasks(DAG dag) {
  int i, j;
  double integral_part;
  double op =0;
  int nb_levels=0;
  int *nb_tasks=NULL;
  int nb_tasks_per_level;
  int total_nb_tasks=0;
  int tmp;

  /* compute the perfect number of tasks per levels */
  modf(exp(global.fat * log((double)global.n)), &integral_part);
  nb_tasks_per_level = (int)(integral_part);

  /* assign a number of tasks per level */
  while (1) {
    tmp = getIntRandomNumberAround(nb_tasks_per_level, 100.00 - 100.0
        *global.regular);
    if (total_nb_tasks + tmp > global.n) {
      tmp = global.n - total_nb_tasks;
    }
    nb_tasks=(int*)realloc(nb_tasks, (nb_levels+1)*sizeof(int));
    nb_tasks[nb_levels++] = tmp;
    total_nb_tasks += tmp;
    if (total_nb_tasks >= global.n)
      break;
  }

  /* Put info in the dag structure */
  dag->nb_levels=nb_levels;
  dag->levels=(Task **)calloc(dag->nb_levels, sizeof(Task*));
  dag->nb_tasks_per_level = nb_tasks;
  for (i=0; i<dag->nb_levels; i++) {
    dag->levels[i] = (Task *)calloc(dag->nb_tasks_per_level[i],
        sizeof(Task));
    for (j=0; j<dag->nb_tasks_per_level[i]; j++) {
      dag->levels[i][j] = (Task)calloc(1, sizeof(struct _Task));
      /** Task cost computation                **/
      /** (1) pick a data size (in elements)   **/
      /** (2) pick a complexity                **/
      /** (3) add a factor for N_2 and N_LOG_N **/
      /** (4) multiply (1) by (2) and by (3)   **/
      /** Cost are in flops                    **/

      dag->levels[i][j]->data_size = ((int) getRandomNumberBetween(
          global.mindata, global.maxdata) / 1024) * 1024;

      op = getRandomNumberBetween(64.0, 512.0);

      if (!global.ccr) {
        dag->levels[i][j]->complexity = ((int) getRandomNumberBetween(
            global.mindata, global.maxdata) % 3 + 1);
      } else {
        dag->levels[i][j]->complexity = (int)global.ccr;
      }

      switch (dag->levels[i][j]->complexity) {
      case N_2:
        dag->levels[i][j]->cost = (op * pow(
            dag->levels[i][j]->data_size, 2.0));
        break;
      case N_LOG_N:
        dag->levels[i][j]->cost = (2 * op * pow(
            dag->levels[i][j]->data_size, 2.0)
        * (log(dag->levels[i][j]->data_size)/log(2.0)));
        break;
      case N_3:
        dag->levels[i][j]->cost
        = pow(dag->levels[i][j]->data_size, 3.0);
        break;
      case MIXED:
        fprintf(stderr, "Modulo error in complexity function\n");
        break;
      }

      dag->levels[i][j]->alpha = getRandomNumberBetween(global.minalpha,
          global.maxalpha);
    }
  }
}

void freeDAG (DAG dag){
  int i,j;
  for (i=0; i<dag->nb_levels; i++) {
    for (j=0; j<dag->nb_tasks_per_level[i]; j++) {
      free(dag->levels[i][j]->transfer_tags);
      free(dag->levels[i][j]->children);
      free(dag->levels[i][j]->comm_costs);
      free(dag->levels[i][j]);
    }
    free(dag->levels[i]);
  }
  free(dag->levels);
  free(dag->nb_tasks_per_level);
  free(dag);
}
