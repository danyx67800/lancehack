// Read a photo of the phone's panel and reduce it to text.
//
//   xcrun swift tools/photo-grid.swift grid  <image> [cols] [rows]
//   xcrun swift tools/photo-grid.swift row   <image> <y0-frac> <y1-frac> [min-run-frac]
//
// "grid" prints the average colour of each cell, so the panel can be located
// inside the photo (bezel, black borders, the loader's stripe) without opening
// an image viewer.
//
// "row" averages the strip between two fractions of the image height - a screen
// row - and prints the runs of similar colour along it, as `#rrggbb x0-x1 (w)`
// with all positions as fractions of the image width. That is what reads the
// kernel's 64x32 probe tiles: their order and count are the measurement, and
// the eye is not good at judging either.
//
// Colours are averaged, so a wide run of one colour stays one run; the run
// splitter uses a generous per-channel tolerance because a photo of an LCD
// panel has colour cast and moire.
import Foundation
import CoreGraphics
import ImageIO

func load(_ path: String) -> CGImage {
    let url = URL(fileURLWithPath: path)
    guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
        FileHandle.standardError.write("cannot read \(path)\n".data(using: .utf8)!)
        exit(1)
    }
    return img
}

/// Decode into a plain RGBA8 buffer once; every mode reads from it.
struct Pixels {
    let w: Int, h: Int, buf: [UInt8]

    init(_ img: CGImage) {
        w = img.width
        h = img.height
        var b = [UInt8](repeating: 0, count: w * h * 4)
        let cs = CGColorSpaceCreateDeviceRGB()
        guard let ctx = CGContext(data: &b, width: w, height: h, bitsPerComponent: 8,
                                  bytesPerRow: w * 4, space: cs,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            FileHandle.standardError.write("cannot make context\n".data(using: .utf8)!)
            exit(1)
        }
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
        buf = b
    }

    func rgb(_ x: Int, _ y: Int) -> (Int, Int, Int) {
        let i = (y * w + x) * 4
        return (Int(buf[i]), Int(buf[i + 1]), Int(buf[i + 2]))
    }
}

func hex(_ c: (Int, Int, Int)) -> String {
    String(format: "#%02x%02x%02x", c.0, c.1, c.2)
}

let args = CommandLine.arguments
guard args.count >= 3 else {
    print("usage: photo-grid.swift grid <image> [cols] [rows] | row <image> <y0> <y1>")
    exit(2)
}
let mode = args[1]
let px = Pixels(load(args[2]))

switch mode {
case "grid":
    let cols = args.count > 3 ? Int(args[3])! : 20
    let rows = args.count > 4 ? Int(args[4])! : 30
    print("\(px.w)x\(px.h) as \(cols)x\(rows)")
    for r in 0..<rows {
        var line = ""
        for c in 0..<cols {
            let x0 = c * px.w / cols, x1 = max(x0 + 1, (c + 1) * px.w / cols)
            let y0 = r * px.h / rows, y1 = max(y0 + 1, (r + 1) * px.h / rows)
            var sr = 0, sg = 0, sb = 0, n = 0
            for y in stride(from: y0, to: y1, by: 2) {
                for x in stride(from: x0, to: x1, by: 2) {
                    let (r_, g_, b_) = px.rgb(x, y)
                    sr += r_; sg += g_; sb += b_; n += 1
                }
            }
            line += hex((sr / n, sg / n, sb / n)) + " "
        }
        print(String(format: "y%.2f ", Double(r) / Double(rows)) + line)
    }

case "row":
    guard args.count >= 5 else { exit(2) }
    let y0f = Double(args[3])!, y1f = Double(args[4])!
    let tol = args.count > 5 ? Int(args[5])! : 28
    let y0 = max(0, Int(y0f * Double(px.h))), y1 = min(px.h, max(y0 + 1, Int(y1f * Double(px.h))))
    // Average each column over the strip.
    var cols = [(Int, Int, Int)]()
    for x in 0..<px.w {
        var sr = 0, sg = 0, sb = 0, n = 0
        for y in y0..<y1 {
            let (r_, g_, b_) = px.rgb(x, y)
            sr += r_; sg += g_; sb += b_; n += 1
        }
        cols.append((sr / n, sg / n, sb / n))
    }
    // Split into runs, merging runs that are within tolerance of the previous
    // run's mean (photo noise otherwise explodes into hundreds of runs).
    var runs: [(Int, Int, (Int, Int, Int))] = []
    var mean = cols[0], start = 0
    func close(_ a: (Int, Int, Int), _ b: (Int, Int, Int)) -> Bool {
        abs(a.0 - b.0) <= tol && abs(a.1 - b.1) <= tol && abs(a.2 - b.2) <= tol
    }
    for x in 1..<px.w {
        if !close(cols[x], mean) {
            runs.append((start, x, mean))
            mean = cols[x]; start = x
        } else {
            mean = ((mean.0 + cols[x].0) / 2, (mean.1 + cols[x].1) / 2, (mean.2 + cols[x].2) / 2)
        }
    }
    runs.append((start, px.w, mean))
    let minw = args.count > 6 ? Double(args[6])! : 0.004
    print("strip y \(Double(y0) / Double(px.h))-\(Double(y1) / Double(px.h)), \(px.w)px wide")
    for (a, b, c) in runs where Double(b - a) / Double(px.w) >= minw {
        print(String(format: "%@ %5.3f-%5.3f  (w %.3f, %d px)",
                     hex(c), Double(a) / Double(px.w), Double(b) / Double(px.w),
                     Double(b - a) / Double(px.w), b - a))
    }

default:
    print("unknown mode \(mode)")
    exit(2)
}
