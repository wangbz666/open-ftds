# OpenFTDS `.ios` format, schema 1

OpenFTDS uses newline-delimited JSON. Every non-empty line is independently
parseable and represents one event's aggregate for one flush interval.

Required fields:

| Field | Type | Meaning |
|---|---|---|
| `schema` | integer | Format version, currently `1` |
| `captured_at_ns` | integer | Unix epoch timestamp of the snapshot |
| `pid` | integer | Producer process ID |
| `event_id` | integer | Stable application-defined event identifier |
| `module` | string | Logical subsystem |
| `name` | string | Event name |
| `level` | integer | `0` normal, `1` high, `2` critical |
| `count` | integer | Completed measurements in the interval |
| `errors` | integer | Measurements completed with non-zero status |
| `slow` | integer | Measurements at or above the configured slow threshold |
| `slow_threshold_ns` | integer | Slow threshold for this producer; `0` means disabled |
| `total_ns` | integer | Sum of all durations |
| `min_ns` | integer | Exact minimum duration |
| `max_ns` | integer | Exact maximum duration |
| `hist_us_log2` | array[32] | Counts grouped by ceil(log2(duration_us)) |

For histogram bucket `i`, the reported upper bound is `2^i` microseconds.
Values above the final bound are accumulated in bucket 31.

Readers must ignore unknown fields to permit compatible schema evolution.
The two slow-I/O fields were added in OpenFTDS 0.2 and are optional for schema 1
readers; missing values mean zero.
