import math


def calculate_edws_case1_verbose(CT0, S_mem, R_alpha_0_1, D):
    """
    Calculates the total write cost (EDWs) for the case where L1 is not full,
    and prints all intermediate calculation steps.

    Args:
        CT0 (float): The threshold factor for the memtable.
        S_mem (float): The size of the memtable (L0).
        R_alpha_0_1 (float): The average data reduction ratio from L0 to L1.
        D (float): The total size of the initial user writes.

    Returns:
        float: The calculated total write cost (EDWs).
    """
    print("--- Starting Calculation ---")
    
    # Ensure no division by zero if inputs are zero
    if CT0 == 0 or S_mem == 0:
        print("CT0 or S_mem is zero, returning 0.")
        return 0.0

    # C0 represents the capacity of L0
    C0 = CT0 * S_mem
    print(f"1. L0 Capacity (C0 = CT0 * S_mem): {C0:.2f}")
    
    # D_prime is the effective data size after reduction
    D_prime = R_alpha_0_1 * D
    print(f"2. Effective Data Size (D' = R_alpha_0_1 * D): {D_prime:.2f}")

    # Calculate p, the total number of compactions from L0 to L1
    # We use math.ceil for the ceiling function
    p = math.ceil(D_prime / C0)
    print(f"3. Number of Compactions (p = ceil(D' / C0)): {p}")

    # Calculate the two components of the EDWs formula
    intermediate_cost = C0 * p * (p - 1) / 2
    print(f"4. Intermediate Cost (C0 * p * (p-1) / 2): {intermediate_cost:.2f}")
    
    final_write_cost = D_prime
    print(f"5. Final Write Cost (D'): {final_write_cost:.2f}")

    # Calculate the total EDWs
    edws = intermediate_cost + final_write_cost
    print("-" * 30)
    print(f"6. Total EDWs (Intermediate Cost + Final Write Cost): {edws:.2f}")
    print("--- Calculation Finished ---")

    # FIX: This return statement is crucial. Without it, the function returns
    # None by default, causing the TypeError you observed.
    return edws

# --- Example Usage ---
if __name__ == "__main__":
    # Define your own parameters here
    # Example values from a hypothetical system
    param_CT0 = 5.0          # Memtable threshold factor
    param_S_mem = 2.8       # Memtable size in MB
    param_R_alpha_0_1 = 0.25  # 20% data reduction
    param_D = 1626         # 1000 MB of total writes

    # Calculate the EDWs using the function
    print("Running EDWs calculation with example parameters:\n")
    
    # Calculate the EDWs using the verbose function
    total_edws = calculate_edws_case1_verbose(
        CT0=param_CT0,
        S_mem=param_S_mem,
        R_alpha_0_1=param_R_alpha_0_1,
        D=param_D
    )
    
    print(f"\nFinal returned value: {total_edws:.2f}")
