//
//  DriverConnector+Isoch.swift
//  ASFW
//
//  Isoch Receive and Transmit control and metrics
//

import Foundation
import IOKit

// MARK: - Isoch RX Metrics Model

/// Isoch Receive metrics snapshot (matches C++ IsochRxSnapshot exactly: 88 bytes)
struct IsochRxMetrics {
    var totalPackets: UInt64 = 0
    var dataPackets: UInt64 = 0      // 80-byte with samples
    var emptyPackets: UInt64 = 0     // 16-byte empty
    var drops: UInt64 = 0            // DBC discontinuities  
    var errors: UInt64 = 0           // CIP parse errors
    
    // Latency histogram [<100µs, 100-500µs, 500-1000µs, >1000µs]
    var latencyHist: (UInt64, UInt64, UInt64, UInt64) = (0, 0, 0, 0)
    
    // Last poll cycle
    var lastPollLatencyUs: UInt32 = 0
    var lastPollPackets: UInt32 = 0
    
    // CIP header snapshot
    var cipSID: UInt8 = 0
    var cipDBS: UInt8 = 0
    var cipFDF: UInt8 = 0
    var cipSYT: UInt16 = 0xFFFF
    var cipDBC: UInt8 = 0
    
    // Computed properties
    var packetsPerSecond: Double {
        // This is an instant value, not real rate. GUI should compute from delta.
        0
    }
    
    var dataRatio: Double {
        guard totalPackets > 0 else { return 0 }
        return Double(dataPackets) / Double(totalPackets)
    }
}

// MARK: - Isoch TX Metrics Model

/// Isoch Transmit metrics snapshot (matches C++ IsochTxSnapshot exactly: 88 bytes)
struct IsochTxMetrics {
    var packetsAssembled: UInt64 = 0  // Total assembled (data + no-data)
    var dataPackets: UInt64 = 0       // Packets with PCM audio frames
    var noDataPackets: UInt64 = 0     // Packets with silence/empty payload
    var underrunCount: UInt64 = 0     // Ring buffer underruns (0 = data flowing OK)

    var bufferFillLevel: UInt32 = 0   // Ring buffer fill % (0-100)
    var zeroCopyEnabled: UInt32 = 0   // 1=zero-copy from CoreAudio, 0=ring-buffer path

    var state: UInt32 = 0             // ITState: 0=Unconfigured 1=Configured 2=Running 3=Stopped
    var maxRefillLatencyUs: UInt32 = 0 // Peak DMA refill latency in µs

    // Refill latency histogram [<50µs, 50-200µs, 200-500µs, >500µs]
    var latencyHist: (UInt64, UInt64, UInt64, UInt64) = (0, 0, 0, 0)

    var irqWatchdogKicks: UInt64 = 0  // IRQ stall recovery kicks

    // Computed
    var stateDescription: String {
        switch state {
        case 0: return "Unconfigured"
        case 1: return "Configured"
        case 2: return "Running"
        case 3: return "Stopped"
        default: return "Unknown(\(state))"
        }
    }

    var dataRatio: Double {
        guard packetsAssembled > 0 else { return 0 }
        return Double(dataPackets) / Double(packetsAssembled)
    }

    var isRunning: Bool { state == 2 }
    var isHealthy: Bool { isRunning && underrunCount == 0 }
}

// MARK: - Driver Connector Extension

extension ASFWDriverConnector {
    
    // MARK: - Isoch Receive Control
    
    /// Start isochronous receive on specified channel
    func startIsochReceive(channel: UInt8) -> Bool {
        guard isConnected, connection != 0 else { return false }
        
        var input: [UInt64] = [UInt64(channel)]
        let kr = IOConnectCallScalarMethod(
            connection,
            Method.startIsochReceive.rawValue,
            &input, 1,
            nil, nil
        )
        
        if kr != KERN_SUCCESS {
            log("startIsochReceive failed: \(interpretIOReturn(kr))", level: .error)
            return false
        }
        log("Started isoch receive on channel \(channel)", level: .info)
        return true
    }
    
    /// Stop isochronous receive
    func stopIsochReceive() -> Bool {
        guard isConnected, connection != 0 else { return false }
        
        let kr = IOConnectCallScalarMethod(
            connection,
            Method.stopIsochReceive.rawValue,
            nil, 0,
            nil, nil
        )
        
        if kr != KERN_SUCCESS {
            log("stopIsochReceive failed: \(interpretIOReturn(kr))", level: .error)
            return false
        }
        log("Stopped isoch receive", level: .info)
        return true
    }
    
    // MARK: - Isoch RX Metrics
    
    /// Fetch current isoch receive metrics from driver
    func getIsochRxMetrics() -> IsochRxMetrics? {
        guard isConnected, connection != 0 else { return nil }
        
        guard let data = callStruct(.getIsochRxMetrics, initialCap: 128) else {
            log("getIsochRxMetrics: callStruct failed", level: .warning)
            return nil
        }
        
        guard data.count >= 88 else {
            log("getIsochRxMetrics: Invalid data size \(data.count)", level: .warning)
            return nil
        }
        
        return data.withUnsafeBytes { ptr -> IsochRxMetrics in
            var m = IsochRxMetrics()
            let base = ptr.baseAddress!
            
            m.totalPackets = base.load(fromByteOffset: 0, as: UInt64.self)
            m.dataPackets = base.load(fromByteOffset: 8, as: UInt64.self)
            m.emptyPackets = base.load(fromByteOffset: 16, as: UInt64.self)
            m.drops = base.load(fromByteOffset: 24, as: UInt64.self)
            m.errors = base.load(fromByteOffset: 32, as: UInt64.self)
            
            m.latencyHist = (
                base.load(fromByteOffset: 40, as: UInt64.self),
                base.load(fromByteOffset: 48, as: UInt64.self),
                base.load(fromByteOffset: 56, as: UInt64.self),
                base.load(fromByteOffset: 64, as: UInt64.self)
            )
            
            m.lastPollLatencyUs = base.load(fromByteOffset: 72, as: UInt32.self)
            m.lastPollPackets = base.load(fromByteOffset: 76, as: UInt32.self)
            
            m.cipSID = base.load(fromByteOffset: 80, as: UInt8.self)
            m.cipDBS = base.load(fromByteOffset: 81, as: UInt8.self)
            m.cipFDF = base.load(fromByteOffset: 82, as: UInt8.self)
            // pad1 at 83
            m.cipSYT = base.load(fromByteOffset: 84, as: UInt16.self)
            m.cipDBC = base.load(fromByteOffset: 86, as: UInt8.self)
            // pad2 at 87
            
            
            return m
        }
    }
    
    // MARK: - Isoch TX Metrics

    /// Fetch current isoch transmit metrics from driver.
    /// Works regardless of whether IT was started manually or by CoreAudio.
    func getIsochTxMetrics() -> IsochTxMetrics? {
        guard isConnected, connection != 0 else { return nil }

        guard let data = callStruct(.getIsochTxMetrics, initialCap: 128) else {
            log("getIsochTxMetrics: callStruct failed", level: .warning)
            return nil
        }

        guard data.count >= 88 else {
            log("getIsochTxMetrics: Invalid data size \(data.count)", level: .warning)
            return nil
        }

        return data.withUnsafeBytes { ptr -> IsochTxMetrics in
            var m = IsochTxMetrics()
            let base = ptr.baseAddress!

            m.packetsAssembled   = base.load(fromByteOffset:  0, as: UInt64.self)
            m.dataPackets        = base.load(fromByteOffset:  8, as: UInt64.self)
            m.noDataPackets      = base.load(fromByteOffset: 16, as: UInt64.self)
            m.underrunCount      = base.load(fromByteOffset: 24, as: UInt64.self)
            m.bufferFillLevel    = base.load(fromByteOffset: 32, as: UInt32.self)
            m.zeroCopyEnabled    = base.load(fromByteOffset: 36, as: UInt32.self)
            m.state              = base.load(fromByteOffset: 40, as: UInt32.self)
            m.maxRefillLatencyUs = base.load(fromByteOffset: 44, as: UInt32.self)
            m.latencyHist = (
                base.load(fromByteOffset: 48, as: UInt64.self),
                base.load(fromByteOffset: 56, as: UInt64.self),
                base.load(fromByteOffset: 64, as: UInt64.self),
                base.load(fromByteOffset: 72, as: UInt64.self)
            )
            m.irqWatchdogKicks   = base.load(fromByteOffset: 80, as: UInt64.self)

            return m
        }
    }

    /// Reset isochronous receive metrics
    func resetIsochRxMetrics() -> Bool {
        guard isConnected, connection != 0 else { return false }
        
        let kr = IOConnectCallScalarMethod(
            connection,
            Method.resetIsochRxMetrics.rawValue,
            nil, 0,
            nil, nil
        )
        
        if kr != KERN_SUCCESS {
            log("resetIsochRxMetrics failed: \(interpretIOReturn(kr))", level: .error)
            return false
        }
        log("Reset isoch metrics", level: .info)
        return true
    }
}
