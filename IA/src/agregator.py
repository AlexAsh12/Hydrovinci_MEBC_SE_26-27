"""
Allows to agregate all the data from the sensors in a dynamic way such that we can perform mean computations of the data for the input for the AI model
"""
def init_accumulator(features):
    """
    Initialise l'accumulateur pour une liste de grandeurs.
    """
    return {
        feature: {"sum": 0.0, "n": 0}
        for feature in features
    }


def aggregate_data(feature, value, acc):
    """
    Met à jour l'accumulateur avec une nouvelle mesure.
    """
    if feature not in acc:
        raise ValueError(f"Unknown feature: {feature}")

    acc[feature]["sum"] += value
    acc[feature]["n"] += 1

    return acc