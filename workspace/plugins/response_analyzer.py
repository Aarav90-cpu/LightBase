"""
LightBase Sample Plugin: Response Analyzer
Analyzes API response data using Python's data processing capabilities.
"""
import json

def run(context):
    """Analyze response data and return insights"""
    response = context.get("response", {})
    url = context.get("url", "")
    method = context.get("method", "GET")
    
    # Analyze the response structure
    analysis = {
        "url": url,
        "method": method,
        "response_type": type(response).__name__,
        "top_level_keys": list(response.keys()) if isinstance(response, dict) else [],
        "total_fields": len(response) if isinstance(response, (dict, list)) else 0,
        "data_size_bytes": len(json.dumps(response)),
    }
    
    # Detect nested structures
    if isinstance(response, dict):
        nested = {k: type(v).__name__ for k, v in response.items()}
        analysis["field_types"] = nested
        
        # Find arrays for potential data analysis
        arrays = {k: len(v) for k, v in response.items() if isinstance(v, list)}
        if arrays:
            analysis["arrays_found"] = arrays
            analysis["hint"] = "Install pandas for DataFrame analysis: pip install pandas"
    
    return analysis
