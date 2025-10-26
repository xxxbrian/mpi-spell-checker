#!/usr/bin/env bash

#SBATCH --job-name=spellcheck_%j
#SBATCH --partition=sapphire
#SBATCH --time=02:00:00
#SBATCH --nodes=1
#SBATCH --ntasks=64
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --output=slurm-%j.out
#SBATCH --error=slurm-%j.err

# module load OpenMPI/4.1.4

python batch_srun.py
