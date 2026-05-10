import os

import numpy as np
import pandas as pd
from sklearn.datasets import make_blobs


OUTPUT_DIR = "data"
RANDOM_STATE = 42

DATASET_TIERS = [
    {
        "name": "tier1_small",
        "points": 10_000,
        "dimensions": 8,
        "clusters": 5,
    },
    {
        "name": "tier2_medium",
        "points": 100_000,
        "dimensions": 16,
        "clusters": 8,
    },
    {
        "name": "tier3_large",
        "points": 2_000_000,
        "dimensions": 64,
        "clusters": 10,
    },
]


def generate_dataset(name, points, dimensions, clusters):
    print(f"\nGenerating {name}")
    print(f"Points: {points}")
    print(f"Dimensions: {dimensions}")
    print(f"Clusters: {clusters}")

    X, y = make_blobs(
        n_samples=points,
        n_features=dimensions,
        centers=clusters,
        cluster_std=1.5,
        random_state=RANDOM_STATE,
    )

    X = X.astype(np.float32)

    feature_columns = [f"x{i + 1}" for i in range(dimensions)]
    df = pd.DataFrame(X, columns=feature_columns)
    df["label"] = y.astype(np.int32)

    output_path = os.path.join(OUTPUT_DIR, f"{name}.csv")
    df.to_csv(output_path, index=False)

    size_mb = os.path.getsize(output_path) / (1024 * 1024)

    print(f"Saved: {output_path}")
    print(f"CSV size: {size_mb:.2f} MB")


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for tier in DATASET_TIERS:
        generate_dataset(
            name=tier["name"],
            points=tier["points"],
            dimensions=tier["dimensions"],
            clusters=tier["clusters"],
        )

    print("\nAll datasets generated successfully.")


if __name__ == "__main__":
    main()
