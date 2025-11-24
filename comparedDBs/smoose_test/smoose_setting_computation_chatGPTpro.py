import math

def dp_optimal_structure(N, F, NL, k, workload):
    """
    Dynamic Programming based solution to find the optimal size ratios (ris) and number of runs (nis)
    for Moose based on the DP recursion described in the paper, while considering the workload.
    
    Parameters:
        N: Total capacity (total number of entries)
        F: Buffer size (capacity of Level-0)
        NL: Capacity of the last level
        k: Run number regulator
        workload: A dictionary containing proportions for range lookup (s), update (u), and point lookup (z)
    
    Returns:
        ris: List of size ratios for each level
        nis: List of number of runs for each level
    """
    # Initialize the recursive DP cache (Memoization)
    dp_cache = {}

    def S(Nd, Nr):
        """Recursive DP function to compute minimum cost for given Nd (current level capacity) and Nr (remaining capacity)."""
        if (Nd, Nr) in dp_cache:
            return dp_cache[(Nd, Nr)]
        
        # Base case: No capacity left
        if Nr == 0:
            return 0
        
        min_cost = float('inf')
        optimal_r = None
        
        # Iterate over all possible size ratio r
        for r in range(2, math.ceil(Nr / Nd) + 1):
            # Recursively compute cost for subproblem
            sub_cost = S(r * Nd, Nr - r * Nd) + math.sqrt(r)
            if sub_cost < min_cost:
                min_cost = sub_cost
                optimal_r = r
        
        # Memoize the result
        dp_cache[(Nd, Nr)] = min_cost
        return min_cost

    # Initialize the size ratio and number of runs arrays
    ris = []
    nis = []

    # Determine the workload parameters for range lookup, update, and point lookup
    s = workload.get("range", 0)  # proportion of range lookup operations
    u = workload.get("update", 0)  # proportion of update operations
    z = workload.get("point", 0)  # proportion of point lookup operations
    
    # Based on workload proportions, adjust the run numbers and size ratios
    remaining_capacity = N - NL
    current_capacity = F
    
    while remaining_capacity > 0:
        best_r = None
        best_n = None
        min_cost = float('inf')
        
        # Try all possible capacity splits for each level, considering workload proportions
        for r in range(2, math.ceil(remaining_capacity / current_capacity) + 1):
            cost = S(r * current_capacity, remaining_capacity - r * current_capacity) + math.sqrt(r)
            if cost < min_cost:
                min_cost = cost
                best_r = r
                best_n = int(k * math.sqrt(r))  # Run number regulator
        
        # If best_r is None, the configuration might be invalid; debug and continue
        if best_r is None:
            print(f"Error: No valid best_r found. Remaining Capacity: {remaining_capacity}, Current Capacity: {current_capacity}")
            break
        
        # Store the optimal size ratio and number of runs for this level
        ris.append(best_r)
        nis.append(best_n)
        
        # Update the remaining capacity and current capacity for next level
        remaining_capacity -= best_r * current_capacity
        current_capacity = best_r * current_capacity

    # Final check for ris and nis
    if not ris or not nis:
        print("Warning: No valid configuration found.")
    
    return ris, nis

# Example usage
N = 100000000  # Total entries in the database
F = 20480  # Buffer size (entries in Level-0)
NL = int(0.8 * N)  # Last level capacity as 80% of the total capacity
k = 2  # Run number regulator

# Define workload as a dictionary
workload = {
    "range": 0.33,   # 33% range lookup operations
    "update": 0.33,  # 33% update operations
    "point": 0.33    # 33% point lookup operations
}

ris, nis = dp_optimal_structure(N, F, NL, k, workload)
print("Optimal size ratios:", ris)
print("Optimal number of runs:", nis)
