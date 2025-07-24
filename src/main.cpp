// #include <main.hpp>
// #include <program.hpp>
// #include <iostream>
//
// int main() {
//
//   try {
//     Program program{};
//     program.run();
//   }
//   catch(std::runtime_error e) {
//     std::cerr << "[Exception] " << e.what() << std::endl;
//   }
// }

// Include headers for various force-directed layout algorithms
#include <vector>
#include <string>
#include <random>
#include <set>
#include <utility> // For std::pair
#include <algorithm> // For std::min, std::max, std::shuffle
#include <iostream> // For console output
#include <memory> // For std::unique_ptr

#include <ogdf/fileformats/GraphIO.h>
//
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/GEMLayout.h>
#include <ogdf/energybased/SpringEmbedderKK.h>
#include <ogdf/energybased/DavidsonHarelLayout.h>
#include <ogdf/energybased/StressMinimization.h>
#include <ogdf/energybased/PivotMDS.h>
#include <ogdf/energybased/FastMultipoleEmbedder.h>
#include <ogdf/energybased/FastMultipoleEmbedder.h>
#include <ogdf/energybased/SpringEmbedderGridVariant.h>

#include <vector>
#include <string>
#include <random>
#include <set>
#include <utility> // For std::pair
#include <algorithm> // For std::min, std::max, std::shuffle
#include <iostream> // For console output
#include <memory> // For std::unique_ptr

int main() {
    // Define graph parameters
    const int numNodes = 500;
    const int numEdges = 1000;

    // 1. Create a graph
    ogdf::Graph G;

    // Create nodes
    std::vector<ogdf::node> nodes(numNodes);
    for (int i = 0; i < numNodes; ++i) {
        nodes[i] = G.newNode();
    }

    // Ensure initial connectivity by creating a path/cycle
    // This connects nodes[0] to nodes[1], nodes[1] to nodes[2], ..., nodes[numNodes-2] to nodes[numNodes-1]
    for (int i = 0; i < numNodes - 1; ++i) {
        G.newEdge(nodes[i], nodes[i + 1]);
    }
    // Optionally, close the cycle to ensure stronger connectivity for small graphs
    // For 500 nodes, a path is sufficient for connectivity, but a cycle is also fine.
    if (numNodes > 1) {
        G.newEdge(nodes[numNodes - 1], nodes[0]);
    }

    // Use a set to keep track of existing edges to avoid duplicates
    // Store pairs canonically (smaller index first)
    std::set<std::pair<int, int>> existingEdges;
    for (int i = 0; i < numNodes - 1; ++i) {
        existingEdges.insert({std::min(i, i + 1), std::max(i, i + 1)});
    }
    if (numNodes > 1) {
        existingEdges.insert({std::min(numNodes - 1, 0), std::max(numNodes - 1, 0)});
    }

    // Add remaining edges randomly until numEdges is reached
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, numNodes - 1);

    int currentEdges = G.numberOfEdges();
    while (currentEdges < numEdges) {
        int u_idx = distrib(gen);
        int v_idx = distrib(gen);

        if (u_idx == v_idx) { // Avoid self-loops
            continue;
        }

        // Ensure (u_idx, v_idx) is canonical (smaller index first)
        if (u_idx > v_idx) {
            std::swap(u_idx, v_idx);
        }

        // Add edge only if it doesn't already exist
        if (existingEdges.find({u_idx, v_idx}) == existingEdges.end()) {
            G.newEdge(nodes[u_idx], nodes[v_idx]);
            existingEdges.insert({u_idx, v_idx});
            currentEdges++;
        }
    }
    std::cout << "Graph created with " << G.numberOfNodes() << " nodes and "
              << G.numberOfEdges() << " edges." << std::endl;

    // 2. List of force-directed layout algorithms to apply
    std::vector<std::pair<std::string, std::unique_ptr<ogdf::LayoutModule>>> layouts;

    // Populate the list with instances of each algorithm
    layouts.push_back({"FMMMLayout", std::make_unique<ogdf::FMMMLayout>()});
    layouts.push_back({"GEMLayout", std::make_unique<ogdf::GEMLayout>()});
    layouts.push_back({"SpringEmbedderKK", std::make_unique<ogdf::SpringEmbedderKK>()});
    layouts.push_back({"DavidsonHarelLayout", std::make_unique<ogdf::DavidsonHarelLayout>()});
    layouts.push_back({"StressMinimization", std::make_unique<ogdf::StressMinimization>()});
    layouts.push_back({"PivotMDS", std::make_unique<ogdf::PivotMDS>()});
    layouts.push_back({"FastMultipoleEmbedder", std::make_unique<ogdf::FastMultipoleEmbedder>()});
    layouts.push_back({"FastMultipoleMultilevelEmbedder", std::make_unique<ogdf::FastMultipoleMultilevelEmbedder>()});
    layouts.push_back({"SpringEmbedderGridVariant", std::make_unique<ogdf::SpringEmbedderGridVariant>()});

    // Iterate through each layout algorithm
    for (const auto& entry : layouts) {
        const std::string& name = entry.first;
        ogdf::LayoutModule* layoutAlgo = entry.second.get(); // Get raw pointer from unique_ptr

        // Create a new GraphAttributes object for each layout
        // This ensures each algorithm starts with a fresh canvas and random initial coordinates
        ogdf::GraphAttributes GA(G,
                                 ogdf::GraphAttributes::nodeGraphics |
                                 ogdf::GraphAttributes::edgeGraphics |
                                 ogdf::GraphAttributes::nodeStyle |
                                 ogdf::GraphAttributes::edgeStyle |
                                 ogdf::GraphAttributes::nodeLabel);


        // Set initial random positions for nodes. This is crucial for force-directed layouts.
        // Replaced GA.randomizeCoordinates() with manual random coordinate assignment
        std::uniform_real_distribution<> coord_distrib(-500.0, 500.0); // Adjust range as needed
        for (ogdf::node v : G.nodes) {
            GA.x(v) = coord_distrib(gen);
            GA.y(v) = coord_distrib(gen);
        }

        // Give each node a visible shape, size and colors
        const double nodeSize = 12.0;
        for (ogdf::node v : G.nodes) {
            GA.width(v)        = nodeSize;
            GA.height(v)       = nodeSize;
            GA.shape(v)        = ogdf::Shape::Ellipse;
            GA.fillColor(v)    = ogdf::Color(255,255,255);
            GA.strokeColor(v)  = ogdf::Color(0,0,0);
        }

        // Apply specific settings for certain algorithms if desired
        if (name == "FMMMLayout") {
            ogdf::FMMMLayout* fmmm = dynamic_cast<ogdf::FMMMLayout*>(layoutAlgo);
            if (fmmm) {
                fmmm->useHighLevelOptions(true);
                fmmm->unitEdgeLength(15.0);
                fmmm->newInitialPlacement(true);
                fmmm->qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
            }
        } else if (name == "SpringEmbedderKK") {
            ogdf::SpringEmbedderKK* kk = dynamic_cast<ogdf::SpringEmbedderKK*>(layoutAlgo);
            if (kk) {
                // Corrected method name based on compiler suggestion
                kk->setDesLength(50.0); // Example setting for desired edge length
            }
        }
        // Add more specific settings for other algorithms here if needed

        std::cout << "Applying " << name << " layout..." << std::endl;
        try {
            layoutAlgo->call(GA); // Call the layout algorithm
            std::string filename = "output-" + name + ".svg";
            ogdf::GraphIO::write(GA, filename, ogdf::GraphIO::drawSVG); // Save the layout as SVG
            std::cout << "Saved layout to " << filename << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error applying " << name << " layout: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error applying " << name << " layout." << std::endl;
        }
    }

    return 0;
}

