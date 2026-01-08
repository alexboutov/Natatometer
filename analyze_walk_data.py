"""
Natatometer Data Analysis and Calibration Script

Analyzes walking test data from ESP32 DataLogger to determine
optimal targetRMS and feedback thresholds.

Usage:
    python analyze_walk_data.py <csv_file>
    python analyze_walk_data.py  (uses default path)

Output:
    - RMS vs time plot with ±1σ bands
    - RMS histogram with threshold markers
    - Session statistics and calibration recommendations
"""

import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# Default data file path
DEFAULT_CSV = r"test-data\walk_data_simulated.csv"


def load_and_clean_data(filepath):
    """Load CSV and separate into sessions, filtering out bad data."""
    
    print(f"Loading: {filepath}")
    
    # Read raw file to handle session markers
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    sessions = []
    current_session = []
    header = None
    
    for line in lines:
        line = line.strip()
        
        # Skip empty lines
        if not line:
            continue
            
        # Capture header
        if line.startswith('timestamp_ms'):
            header = line.split(',')
            continue
        
        # Session markers
        if line == '# SESSION_START':
            current_session = []
            continue
        elif line == '# SESSION_END':
            if current_session:
                sessions.append(current_session)
            continue
        
        # Skip other comments
        if line.startswith('#') or line.startswith('='):
            continue
        
        # Data line
        try:
            values = line.split(',')
            if len(values) >= 6:
                current_session.append(values)
        except:
            continue
    
    # Don't forget last session if file doesn't end with SESSION_END
    if current_session:
        sessions.append(current_session)
    
    print(f"Found {len(sessions)} session(s)")
    
    # Convert to DataFrames
    session_dfs = []
    for i, session in enumerate(sessions):
        df = pd.DataFrame(session, columns=header)
        
        # Convert to numeric
        for col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
        
        # Filter out bad timestamps (integer underflow fix)
        df = df[df['timestamp_ms'] < 1e9]
        df = df[df['timestamp_ms'] >= 0]
        
        # Convert to seconds
        df['time_s'] = df['timestamp_ms'] / 1000.0
        
        # Add session ID
        df['session'] = i + 1
        
        session_dfs.append(df)
        print(f"  Session {i+1}: {len(df)} samples, {df['time_s'].max():.1f}s duration")
    
    # Combine all sessions
    all_data = pd.concat(session_dfs, ignore_index=True)
    
    return all_data, session_dfs


def compute_statistics(df, label=""):
    """Compute summary statistics for RMS values."""
    
    rms = df['rms'].dropna()
    
    stats = {
        'label': label,
        'count': len(rms),
        'duration_s': df['time_s'].max() - df['time_s'].min(),
        'mean': rms.mean(),
        'std': rms.std(),
        'min': rms.min(),
        'max': rms.max(),
        'median': rms.median(),
        'p10': rms.quantile(0.10),
        'p90': rms.quantile(0.90),
    }
    
    return stats


def print_statistics(stats):
    """Print formatted statistics."""
    
    print(f"\n{'='*50}")
    print(f"Statistics: {stats['label']}")
    print(f"{'='*50}")
    print(f"  Samples:    {stats['count']}")
    print(f"  Duration:   {stats['duration_s']:.1f} s")
    print(f"  Mean RMS:   {stats['mean']:.3f} m/s²")
    print(f"  Std Dev:    {stats['std']:.3f} m/s²")
    print(f"  Min:        {stats['min']:.3f} m/s²")
    print(f"  Max:        {stats['max']:.3f} m/s²")
    print(f"  Median:     {stats['median']:.3f} m/s²")
    print(f"  10th %ile:  {stats['p10']:.3f} m/s²")
    print(f"  90th %ile:  {stats['p90']:.3f} m/s²")


def compute_calibration(stats):
    """Compute recommended calibration values."""
    
    target_rms = stats['mean']
    
    # Use coefficient of variation to set thresholds
    cv = stats['std'] / stats['mean']
    
    # Thresholds should be outside normal variation
    # Use ~1.5 sigma as threshold
    threshold_pct = max(0.10, min(0.25, cv * 1.5))
    
    calibration = {
        'target_rms': target_rms,
        'threshold_pct': threshold_pct,
        'fast_threshold': 1.0 + threshold_pct,
        'slow_threshold': 1.0 - threshold_pct,
        'fast_rms': target_rms * (1.0 + threshold_pct),
        'slow_rms': target_rms * (1.0 - threshold_pct),
    }
    
    return calibration


def print_calibration(cal):
    """Print calibration recommendations."""
    
    print(f"\n{'='*50}")
    print("CALIBRATION RECOMMENDATIONS")
    print(f"{'='*50}")
    print(f"\nFor Natatometer.ino, set:")
    print(f"  float targetRMS = {cal['target_rms']:.2f};")
    print(f"  float fastThreshold = {cal['fast_threshold']:.2f};  // +{cal['threshold_pct']*100:.0f}%")
    print(f"  float slowThreshold = {cal['slow_threshold']:.2f};  // -{cal['threshold_pct']*100:.0f}%")
    print(f"\nFeedback triggers:")
    print(f"  FAST beep when RMS > {cal['fast_rms']:.2f} m/s²")
    print(f"  SLOW beep when RMS < {cal['slow_rms']:.2f} m/s²")


def plot_rms_timeline(session_dfs, calibration, output_path=None):
    """Plot RMS vs time for each session with threshold bands."""
    
    n_sessions = len(session_dfs)
    fig, axes = plt.subplots(n_sessions, 1, figsize=(12, 4*n_sessions), squeeze=False)
    
    target = calibration['target_rms']
    fast_rms = calibration['fast_rms']
    slow_rms = calibration['slow_rms']
    
    for i, df in enumerate(session_dfs):
        ax = axes[i, 0]
        
        time = df['time_s']
        rms = df['rms']
        
        # Plot RMS
        ax.plot(time, rms, 'b-', linewidth=0.8, alpha=0.7, label='RMS')
        
        # Target line
        ax.axhline(y=target, color='green', linestyle='-', linewidth=2, label=f'Target ({target:.2f})')
        
        # Threshold bands
        ax.axhline(y=fast_rms, color='red', linestyle='--', linewidth=1.5, label=f'Fast ({fast_rms:.2f})')
        ax.axhline(y=slow_rms, color='orange', linestyle='--', linewidth=1.5, label=f'Slow ({slow_rms:.2f})')
        
        # Shade threshold regions
        ax.axhspan(fast_rms, rms.max()*1.1, alpha=0.1, color='red')
        ax.axhspan(0, slow_rms, alpha=0.1, color='orange')
        
        # ±1σ band around mean
        mean = rms.mean()
        std = rms.std()
        ax.fill_between(time, mean-std, mean+std, alpha=0.2, color='blue', label=f'±1σ ({std:.2f})')
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Acceleration RMS (m/s²)')
        ax.set_title(f'Session {i+1}: RMS Timeline')
        ax.legend(loc='upper right')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, max(2.5, rms.max()*1.1))
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved: {output_path}")
    
    return fig


def plot_rms_histogram(all_data, calibration, output_path=None):
    """Plot histogram of RMS values with threshold markers."""
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    rms = all_data['rms'].dropna()
    target = calibration['target_rms']
    fast_rms = calibration['fast_rms']
    slow_rms = calibration['slow_rms']
    
    # Histogram
    n, bins, patches = ax.hist(rms, bins=50, density=True, alpha=0.7, color='steelblue', edgecolor='white')
    
    # Color bins by threshold region
    for patch, left_edge in zip(patches, bins[:-1]):
        if left_edge > fast_rms:
            patch.set_facecolor('red')
            patch.set_alpha(0.5)
        elif left_edge < slow_rms:
            patch.set_facecolor('orange')
            patch.set_alpha(0.5)
    
    # Vertical lines for thresholds
    ax.axvline(x=target, color='green', linestyle='-', linewidth=2, label=f'Target ({target:.2f})')
    ax.axvline(x=fast_rms, color='red', linestyle='--', linewidth=2, label=f'Fast threshold ({fast_rms:.2f})')
    ax.axvline(x=slow_rms, color='orange', linestyle='--', linewidth=2, label=f'Slow threshold ({slow_rms:.2f})')
    
    # Statistics annotation
    stats_text = f'Mean: {rms.mean():.2f}\nStd: {rms.std():.2f}\nN: {len(rms)}'
    ax.text(0.95, 0.95, stats_text, transform=ax.transAxes, fontsize=10,
            verticalalignment='top', horizontalalignment='right',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    ax.set_xlabel('Acceleration RMS (m/s²)')
    ax.set_ylabel('Density')
    ax.set_title('Distribution of RMS Values')
    ax.legend(loc='upper left')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved: {output_path}")
    
    return fig


def plot_accel_components(session_dfs, output_path=None):
    """Plot acceleration components over time."""
    
    n_sessions = len(session_dfs)
    fig, axes = plt.subplots(n_sessions, 1, figsize=(12, 4*n_sessions), squeeze=False)
    
    for i, df in enumerate(session_dfs):
        ax = axes[i, 0]
        
        time = df['time_s']
        
        ax.plot(time, df['accel_x'], 'r-', linewidth=0.5, alpha=0.7, label='X')
        ax.plot(time, df['accel_y'], 'g-', linewidth=0.5, alpha=0.7, label='Y')
        ax.plot(time, df['accel_z'], 'b-', linewidth=0.5, alpha=0.7, label='Z')
        ax.plot(time, df['accel_mag'], 'k-', linewidth=1, alpha=0.9, label='Magnitude')
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Acceleration (m/s²)')
        ax.set_title(f'Session {i+1}: Acceleration Components')
        ax.legend(loc='upper right')
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved: {output_path}")
    
    return fig


def main():
    """Main analysis workflow."""
    
    # Determine input file
    if len(sys.argv) > 1:
        csv_path = sys.argv[1]
    else:
        csv_path = DEFAULT_CSV
    
    csv_path = Path(csv_path)
    
    if not csv_path.exists():
        print(f"Error: File not found: {csv_path}")
        sys.exit(1)
    
    # Create output directory
    output_dir = csv_path.parent / "analysis"
    output_dir.mkdir(exist_ok=True)
    
    # Load data
    all_data, session_dfs = load_and_clean_data(csv_path)
    
    if len(all_data) == 0:
        print("Error: No valid data found!")
        sys.exit(1)
    
    # Compute statistics
    overall_stats = compute_statistics(all_data, "All Sessions Combined")
    print_statistics(overall_stats)
    
    for i, df in enumerate(session_dfs):
        session_stats = compute_statistics(df, f"Session {i+1}")
        print_statistics(session_stats)
    
    # Compute calibration
    calibration = compute_calibration(overall_stats)
    print_calibration(calibration)
    
    # Generate plots
    print(f"\nGenerating plots in: {output_dir}")
    
    plot_rms_timeline(session_dfs, calibration, 
                      output_dir / "rms_timeline.png")
    
    plot_rms_histogram(all_data, calibration,
                       output_dir / "rms_histogram.png")
    
    plot_accel_components(session_dfs,
                          output_dir / "accel_components.png")
    
    # Show plots
    print("\nDisplaying plots... (close windows to exit)")
    plt.show()
    
    print("\nAnalysis complete!")


if __name__ == "__main__":
    main()
