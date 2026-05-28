//
//  MetricsView.swift
//  ASFW
//
//  Isochronous metrics dashboard with live updates.
//  Shows both IR (Isoch Receive) and IT (Isoch Transmit) metrics.
//  IT tab works even when CoreAudio manages the IT context.
//

import SwiftUI
import Charts
import Combine

// MARK: - Tab Selection

enum IsochTab: String, CaseIterable {
    case receive = "Isoch Receive"
    case transmit = "Isoch Transmit"
}

// MARK: - RX ViewModel

@MainActor
class MetricsViewModel: ObservableObject {
    @Published var metrics: IsochRxMetrics?
    @Published var isReceiving = false
    @Published var packetsPerSecond: Double = 0
    @Published var history: [Double] = []  // Last 30 seconds of pkts/sec

    private var connector: ASFWDriverConnector
    private var timer: Timer?
    private var lastPacketCount: UInt64 = 0
    private var lastTimestamp = Date()

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    func startPolling() {
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            Task { @MainActor [self] in
                self.fetchMetrics()
            }
        }
    }

    func stopPolling() {
        timer?.invalidate()
        timer = nil
    }

    func fetchMetrics() {
        guard let m = connector.getIsochRxMetrics() else { return }

        let now = Date()
        let elapsed = now.timeIntervalSince(lastTimestamp)
        if elapsed > 0 && m.totalPackets > lastPacketCount {
            packetsPerSecond = Double(m.totalPackets - lastPacketCount) / elapsed
        }
        lastPacketCount = m.totalPackets
        lastTimestamp = now

        history.append(packetsPerSecond)
        if history.count > 30 { history.removeFirst() }

        metrics = m
        isReceiving = m.totalPackets > 0
    }

    func startReceive(channel: UInt8 = 0) {
        if connector.startIsochReceive(channel: channel) { isReceiving = true }
    }

    func stopReceive() {
        if connector.stopIsochReceive() { isReceiving = false }
    }

    func resetMetrics() -> Bool {
        let success = connector.resetIsochRxMetrics()
        if success {
            fetchMetrics()
            history.removeAll()
            packetsPerSecond = 0
            lastPacketCount = 0
        }
        return success
    }
}

// MARK: - TX ViewModel

@MainActor
class TxMetricsViewModel: ObservableObject {
    @Published var metrics: IsochTxMetrics?
    @Published var packetsPerSecond: Double = 0
    @Published var history: [Double] = []

    private var connector: ASFWDriverConnector
    private var timer: Timer?
    private var lastPacketCount: UInt64 = 0
    private var lastTimestamp = Date()

    init(connector: ASFWDriverConnector) {
        self.connector = connector
    }

    func startPolling() {
        // Poll immediately on start
        fetchMetrics()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            Task { @MainActor [self] in self.fetchMetrics() }
        }
    }

    func stopPolling() {
        timer?.invalidate()
        timer = nil
    }

    func fetchMetrics() {
        guard let m = connector.getIsochTxMetrics() else { return }

        let now = Date()
        let elapsed = now.timeIntervalSince(lastTimestamp)
        if elapsed > 0 && m.packetsAssembled > lastPacketCount {
            packetsPerSecond = Double(m.packetsAssembled - lastPacketCount) / elapsed
        }
        lastPacketCount = m.packetsAssembled
        lastTimestamp = now

        history.append(packetsPerSecond)
        if history.count > 30 { history.removeFirst() }

        metrics = m
    }
}

// MARK: - Main View

struct MetricsView: View {
    @StateObject private var rxViewModel: MetricsViewModel
    @StateObject private var txViewModel: TxMetricsViewModel
    @State private var selectedTab: IsochTab = .receive

    init(connector: ASFWDriverConnector) {
        _rxViewModel = StateObject(wrappedValue: MetricsViewModel(connector: connector))
        _txViewModel = StateObject(wrappedValue: TxMetricsViewModel(connector: connector))
    }

    var body: some View {
        VStack(spacing: 16) {
            // Tab Bar
            HStack {
                ForEach(IsochTab.allCases, id: \.self) { tab in
                    Button(action: { selectedTab = tab }) {
                        TabButton(title: tab.rawValue, isSelected: selectedTab == tab)
                    }
                    .buttonStyle(.plain)
                }
                Spacer()

                // Controls only for RX tab (manual start/stop)
                if selectedTab == .receive {
                    Button(action: {
                        if rxViewModel.isReceiving {
                            rxViewModel.stopReceive()
                        } else {
                            rxViewModel.startReceive()
                        }
                    }) {
                        HStack {
                            Image(systemName: rxViewModel.isReceiving ? "stop.fill" : "play.fill")
                            Text(rxViewModel.isReceiving ? "Stop" : "Start")
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(rxViewModel.isReceiving ? Color.red : Color.green)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                    }

                    Button(action: { _ = rxViewModel.resetMetrics() }) {
                        Image(systemName: "trash")
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(Color.gray.opacity(0.3))
                    .foregroundColor(.white)
                    .cornerRadius(8)
                    .help("Reset Metrics")
                }
            }
            .padding(.horizontal)

            // Tab Content
            switch selectedTab {
            case .receive:
                IsochRxTabContent(viewModel: rxViewModel)
            case .transmit:
                IsochTxTabContent(viewModel: txViewModel)
            }
        }
        .padding(.vertical)
        .onAppear {
            rxViewModel.startPolling()
            txViewModel.startPolling()
        }
        .onDisappear {
            rxViewModel.stopPolling()
            txViewModel.stopPolling()
        }
    }
}

// MARK: - RX Tab Content

struct IsochRxTabContent: View {
    @ObservedObject var viewModel: MetricsViewModel

    var body: some View {
        VStack(spacing: 16) {
            HStack(spacing: 16) {
                StatCard(title: "Throughput",
                        value: String(format: "%.0f", viewModel.packetsPerSecond),
                        unit: "pkts/sec", color: .blue)
                StatCard(title: "Total Packets",
                        value: formatNumber(viewModel.metrics?.totalPackets ?? 0),
                        unit: "", color: .primary)
                StatCard(title: "Drops",
                        value: String(viewModel.metrics?.drops ?? 0),
                        unit: "", color: (viewModel.metrics?.drops ?? 0) == 0 ? .green : .red)
                StatCard(title: "Errors",
                        value: String(viewModel.metrics?.errors ?? 0),
                        unit: "", color: (viewModel.metrics?.errors ?? 0) == 0 ? .green : .orange)
            }
            .padding(.horizontal)

            HStack(spacing: 16) {
                VStack(alignment: .leading) {
                    Text("Poll Latency Distribution").font(.headline)
                    if let m = viewModel.metrics {
                        LatencyHistogram(buckets: [m.latencyHist.0, m.latencyHist.1,
                                                    m.latencyHist.2, m.latencyHist.3])
                            .frame(height: 150)
                    } else {
                        Text("No data").foregroundColor(.secondary).frame(height: 150)
                    }
                }
                .padding()
                .background(Color(.windowBackgroundColor).opacity(0.5))
                .cornerRadius(12)

                VStack(alignment: .leading) {
                    Text("Packet Types").font(.headline)
                    if let m = viewModel.metrics, m.totalPackets > 0 {
                        PacketTypePie(dataPackets: m.dataPackets, emptyPackets: m.emptyPackets)
                            .frame(width: 150, height: 150)
                    } else {
                        Text("No data").foregroundColor(.secondary).frame(width: 150, height: 150)
                    }
                }
                .padding()
                .background(Color(.windowBackgroundColor).opacity(0.5))
                .cornerRadius(12)
            }
            .padding(.horizontal)

            VStack(alignment: .leading) {
                Text("Throughput (last 30s)").font(.headline)
                ThroughputSparkline(data: viewModel.history).frame(height: 80)
            }
            .padding()
            .background(Color(.windowBackgroundColor).opacity(0.5))
            .cornerRadius(12)
            .padding(.horizontal)

            if let m = viewModel.metrics {
                CIPStatusBar(metrics: m).padding(.horizontal)
            }

            Spacer()
        }
    }

    private func formatNumber(_ n: UInt64) -> String {
        if n >= 1_000_000 { return String(format: "%.1fM", Double(n) / 1_000_000) }
        if n >= 1_000 { return String(format: "%.1fK", Double(n) / 1_000) }
        return String(n)
    }
}

// MARK: - TX Tab Content

struct IsochTxTabContent: View {
    @ObservedObject var viewModel: TxMetricsViewModel

    var body: some View {
        VStack(spacing: 16) {
            // Status banner
            if let m = viewModel.metrics {
                TxStatusBanner(metrics: m)
                    .padding(.horizontal)
            }

            // Core counter cards
            HStack(spacing: 16) {
                StatCard(title: "Throughput",
                        value: String(format: "%.0f", viewModel.packetsPerSecond),
                        unit: "pkts/sec", color: .blue)

                let underruns = viewModel.metrics?.underrunCount ?? 0
                StatCard(title: "Underruns",
                        value: String(underruns),
                        unit: underruns == 0 ? "✓ No underruns" : "⚠ Buffer empty",
                        color: underruns == 0 ? .green : .red)

                StatCard(title: "Data Pkts",
                        value: formatNumber(viewModel.metrics?.dataPackets ?? 0),
                        unit: "", color: .primary)

                StatCard(title: "No-Data Pkts",
                        value: formatNumber(viewModel.metrics?.noDataPackets ?? 0),
                        unit: "", color: .secondary)
            }
            .padding(.horizontal)

            // Second row
            HStack(spacing: 16) {
                let fill = viewModel.metrics?.bufferFillLevel ?? 0
                StatCard(title: "Buffer Fill",
                        value: "\(fill)%",
                        unit: "",
                        color: fill > 20 ? .green : (fill > 5 ? .yellow : .red))

                let zcEnabled = (viewModel.metrics?.zeroCopyEnabled ?? 0) != 0
                StatCard(title: "Mode",
                        value: zcEnabled ? "Zero-Copy" : "Ring-Buffer",
                        unit: zcEnabled ? "CoreAudio direct" : "Indirect copy",
                        color: zcEnabled ? .green : .yellow)

                StatCard(title: "Max Latency",
                        value: "\(viewModel.metrics?.maxRefillLatencyUs ?? 0) µs",
                        unit: "DMA refill",
                        color: (viewModel.metrics?.maxRefillLatencyUs ?? 0) < 500 ? .green : .orange)

                let kicks = viewModel.metrics?.irqWatchdogKicks ?? 0
                StatCard(title: "IRQ Watchdog",
                        value: String(kicks),
                        unit: kicks == 0 ? "No stalls" : "Stall recoveries",
                        color: kicks == 0 ? .green : .orange)
            }
            .padding(.horizontal)

            // Latency histogram + throughput
            HStack(spacing: 16) {
                VStack(alignment: .leading) {
                    Text("Refill Latency Distribution").font(.headline)
                    if let m = viewModel.metrics {
                        LatencyHistogram(
                            buckets: [m.latencyHist.0, m.latencyHist.1,
                                      m.latencyHist.2, m.latencyHist.3],
                            labels: ["<50µs", "50-200µs", "200-500µs", ">500µs"]
                        )
                        .frame(height: 150)
                    } else {
                        Text("No data").foregroundColor(.secondary).frame(height: 150)
                    }
                }
                .padding()
                .background(Color(.windowBackgroundColor).opacity(0.5))
                .cornerRadius(12)

                VStack(alignment: .leading) {
                    Text("Data vs No-Data").font(.headline)
                    if let m = viewModel.metrics, m.packetsAssembled > 0 {
                        PacketTypePie(dataPackets: m.dataPackets, emptyPackets: m.noDataPackets)
                            .frame(width: 150, height: 150)
                    } else {
                        Text("No data").foregroundColor(.secondary).frame(width: 150, height: 150)
                    }
                }
                .padding()
                .background(Color(.windowBackgroundColor).opacity(0.5))
                .cornerRadius(12)
            }
            .padding(.horizontal)

            VStack(alignment: .leading) {
                Text("TX Throughput (last 30s)").font(.headline)
                ThroughputSparkline(data: viewModel.history).frame(height: 80)
            }
            .padding()
            .background(Color(.windowBackgroundColor).opacity(0.5))
            .cornerRadius(12)
            .padding(.horizontal)

            Spacer()
        }
    }

    private func formatNumber(_ n: UInt64) -> String {
        if n >= 1_000_000 { return String(format: "%.1fM", Double(n) / 1_000_000) }
        if n >= 1_000 { return String(format: "%.1fK", Double(n) / 1_000) }
        return String(n)
    }
}

// MARK: - TX Status Banner

struct TxStatusBanner: View {
    let metrics: IsochTxMetrics

    var body: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(statusColor)
                .frame(width: 10, height: 10)

            Text("IT Context: \(metrics.stateDescription)")
                .font(.caption.bold())

            Spacer()

            if metrics.underrunCount > 0 {
                Label("\(metrics.underrunCount) underruns", systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundColor(.red)
            } else if metrics.isRunning {
                Label("Data flowing", systemImage: "checkmark.circle.fill")
                    .font(.caption)
                    .foregroundColor(.green)
            }

            Text(metrics.zeroCopyEnabled != 0 ? "zero-copy" : "ring-buffer")
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color.black.opacity(0.3))
        .cornerRadius(8)
    }

    private var statusColor: Color {
        switch metrics.state {
        case 2: return metrics.underrunCount == 0 ? .green : .orange
        case 1: return .yellow
        case 3: return .gray
        default: return .gray
        }
    }
}

// MARK: - Subviews

struct TabButton: View {
    let title: String
    let isSelected: Bool

    var body: some View {
        Text(title)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(isSelected ? Color.accentColor : Color.clear)
            .foregroundColor(isSelected ? .white : .secondary)
            .cornerRadius(8)
    }
}

struct StatCard: View {
    let title: String
    let value: String
    let unit: String
    let color: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value)
                .font(.system(size: 28, weight: .bold, design: .rounded))
                .foregroundColor(color)
            Text(unit.isEmpty ? title : "\(unit)")
                .font(.caption)
                .foregroundColor(.secondary)
            if !unit.isEmpty {
                Text(title)
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color(.windowBackgroundColor).opacity(0.5))
        .cornerRadius(12)
    }
}

struct LatencyHistogram: View {
    let buckets: [UInt64]
    var labels: [String] = ["<100µs", "100-500µs", "500-1ms", ">1ms"]

    var body: some View {
        Chart(Array(zip(labels.indices, labels)), id: \.0) { index, label in
            BarMark(
                x: .value("Bucket", label),
                y: .value("Count", buckets.indices.contains(index) ? buckets[index] : 0)
            )
            .foregroundStyle(
                index == 0 ? Color.green :
                index == 1 ? Color.yellow :
                index == 2 ? Color.orange : Color.red
            )
        }
        .chartYAxis { AxisMarks(position: .leading) }
    }
}

struct PacketTypePie: View {
    let dataPackets: UInt64
    let emptyPackets: UInt64

    var body: some View {
        let total = max(1, dataPackets + emptyPackets)
        let dataRatio = Double(dataPackets) / Double(total)

        VStack {
            ZStack {
                Circle()
                    .trim(from: 0, to: CGFloat(dataRatio))
                    .stroke(Color.blue, lineWidth: 30)
                    .rotationEffect(.degrees(-90))
                Circle()
                    .trim(from: CGFloat(dataRatio), to: 1)
                    .stroke(Color.gray.opacity(0.5), lineWidth: 30)
                    .rotationEffect(.degrees(-90))
                VStack {
                    Text("\(Int(dataRatio * 100))%")
                        .font(.title2.bold())
                    Text("Data")
                        .font(.caption)
                }
            }
            HStack(spacing: 16) {
                Label("Data", systemImage: "circle.fill").font(.caption).foregroundColor(.blue)
                Label("Empty", systemImage: "circle.fill").font(.caption).foregroundColor(.gray)
            }
        }
    }
}

struct ThroughputSparkline: View {
    let data: [Double]

    var body: some View {
        if data.isEmpty {
            Text("Collecting data...").foregroundColor(.secondary)
        } else {
            Chart(Array(data.enumerated()), id: \.offset) { index, value in
                LineMark(x: .value("Time", index), y: .value("Pkts/sec", value))
                    .foregroundStyle(Color.blue)
                AreaMark(x: .value("Time", index), y: .value("Pkts/sec", value))
                    .foregroundStyle(LinearGradient(
                        colors: [Color.blue.opacity(0.3), Color.blue.opacity(0.0)],
                        startPoint: .top, endPoint: .bottom))
            }
            .chartYAxis { AxisMarks(position: .leading) }
            .chartXAxis(.hidden)
        }
    }
}

struct CIPStatusBar: View {
    let metrics: IsochRxMetrics

    var body: some View {
        HStack {
            Text("CIP:").font(.caption.bold())
            Group {
                Text("SID=\(metrics.cipSID)")
                Text("DBS=\(metrics.cipDBS)")
                Text(String(format: "FDF=0x%02X", metrics.cipFDF))
                Text(String(format: "SYT=0x%04X", metrics.cipSYT))
                Text(String(format: "DBC=0x%02X", metrics.cipDBC))
            }
            .font(.system(.caption, design: .monospaced))
            Spacer()
            Circle()
                .fill(metrics.totalPackets > 0 ? Color.green : Color.gray)
                .frame(width: 8, height: 8)
            Text("Live").font(.caption)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color.black.opacity(0.3))
        .cornerRadius(8)
    }
}

#Preview {
    MetricsView(connector: ASFWDriverConnector())
        .frame(width: 800, height: 600)
}
