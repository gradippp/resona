import React, { useEffect, useRef } from 'react';

interface WaveformPoint {
  minPeak: number;
  maxPeak: number;
}

interface Props {
  /**
   * Base64 encoded string of int16_t pairs (minPeak, maxPeak)
   */
  waveformPeaksB64: string;
  width?: number;
  height?: number;
  color?: string;
}

/**
 * Professional Symmetric Waveform Renderer for Next.js
 * Decodes base64 quantized int16 peaks and renders to a high-DPI canvas.
 */
const WaveformRenderer: React.FC<Props> = ({
  waveformPeaksB64,
  width = 800,
  height = 200,
  color = '#3b82f6', // Tailwind blue-500
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !waveformPeaksB64) return;

    // Decode base64 to binary
    const binaryString = window.atob(waveformPeaksB64);
    const bytes = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
      bytes[i] = binaryString.charCodeAt(i);
    }

    // Interpret as Int16Array
    // Structure: [min0, max0, min1, max1, ...]
    const peaks = new Int16Array(bytes.buffer);
    const pointCount = peaks.length / 2;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Handle high-DPI displays
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
    ctx.scale(dpr, dpr);

    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = color;

    const centerY = height / 2;
    const barWidth = width / pointCount;

    // Render symmetric peaks
    for (let i = 0; i < pointCount; i++) {
      const minRaw = peaks[i * 2];
      const maxRaw = peaks[i * 2 + 1];

      // Convert back from int16 [-32767, 32767] to normalized [0, 1]
      // Note: minPeak is usually negative, maxPeak positive.
      const min = minRaw / 32767;
      const max = maxRaw / 32767;

      const x = i * barWidth;
      const yMin = centerY + min * (height / 2);
      const yMax = centerY - max * (height / 2);

      // Draw a vertical line representing the envelope at this window
      ctx.fillRect(x, yMax, Math.max(1, barWidth - 1), yMin - yMax);
    }
  }, [waveformPeaksB64, width, height, color]);

  return (
    <div className="relative border border-gray-200 rounded-lg overflow-hidden bg-white">
      <canvas ref={canvasRef} />
    </div>
  );
};

export default WaveformRenderer;

/**
 * Example Usage:
 * 
 * <WaveformRenderer 
 *   waveformPeaksB64={task.waveform_peaks_b64} 
 *   width={1000} 
 *   height={150} 
 * />
 */
