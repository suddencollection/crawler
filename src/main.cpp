#if 1
#include <main.hpp>
#include <program.hpp>
#include <iostream>

int main() {

  try {
    Program program{};
    program.run();
  }
  catch(std::runtime_error e) {
    std::cerr << "[Exception] " << e.what() << std::endl;
  }
}
#else

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <set>
#include <utility>
#include <algorithm>
#include <memory>
#include <map>

// OGDF core includes
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/fileformats/GraphIO.h>
#include <ogdf/basic/geometry.h> // For ogdf::DPoint

// OGDF layout algorithm headers
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/GEMLayout.h>
#include <ogdf/energybased/SpringEmbedderKK.h>
#include <ogdf/energybased/DavidsonHarelLayout.h>
#include <ogdf/energybased/StressMinimization.h>
#include <ogdf/energybased/PivotMDS.h>
#include <ogdf/energybased/FastMultipoleEmbedder.h>
#include <ogdf/energybased/SpringEmbedderGridVariant.h>
#include <ogdf/energybased/MultilevelLayout.h>
#include <ogdf/energybased/multilevel_mixer/RandomPlacer.h>


int main() {
    // Define graph parameters
    const int numNodes = 50;
    const int numEdges = 75;

    // --- 1. Create a Random Graph ---
    ogdf::Graph G;
    std::vector<ogdf::node> nodes(numNodes);
    for (int i = 0; i < numNodes; ++i) {
        nodes[i] = G.newNode();
    }

    // Ensure initial connectivity
    for (int i = 0; i < numNodes - 1; ++i) {
        G.newEdge(nodes[i], nodes[i + 1]);
    }
    if (numNodes > 1) {
        G.newEdge(nodes[numNodes - 1], nodes[0]);
    }

    // Use a set to avoid duplicate edges
    std::set<std::pair<int, int>> existingEdges;
    for (int i = 0; i < numNodes - 1; ++i) {
        existingEdges.insert({std::min(i, i + 1), std::max(i, i + 1)});
    }
    if (numNodes > 1) {
        existingEdges.insert({0, numNodes - 1});
    }

    // Add remaining edges randomly
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, numNodes - 1);
    int currentEdges = G.numberOfEdges();
    while (currentEdges < numEdges) {
        int u_idx = distrib(gen);
        int v_idx = distrib(gen);
        if (u_idx == v_idx) continue;
        if (u_idx > v_idx) std::swap(u_idx, v_idx);
        if (existingEdges.find({u_idx, v_idx}) == existingEdges.end()) {
            G.newEdge(nodes[u_idx], nodes[v_idx]);
            existingEdges.insert({u_idx, v_idx});
            currentEdges++;
        }
    }
    std::cout << "Graph created with " << G.numberOfNodes() << " nodes and "
              << G.numberOfEdges() << " edges." << std::endl;

    // --- 2. List of Layout Algorithms to Apply ---
    std::vector<std::pair<std::string, std::unique_ptr<ogdf::LayoutModule>>> layouts;

    layouts.push_back({"FMMMLayout", std::make_unique<ogdf::FMMMLayout>()});
    layouts.push_back({"GEMLayout", std::make_unique<ogdf::GEMLayout>()});
    layouts.push_back({"SpringEmbedderKK", std::make_unique<ogdf::SpringEmbedderKK>()});
    layouts.push_back({"DavidsonHarelLayout", std::make_unique<ogdf::DavidsonHarelLayout>()});
    layouts.push_back({"StressMinimization", std::make_unique<ogdf::StressMinimization>()});
    layouts.push_back({"PivotMDS", std::make_unique<ogdf::PivotMDS>()});
    layouts.push_back({"FastMultipoleEmbedder", std::make_unique<ogdf::FastMultipoleEmbedder>()});
    layouts.push_back({"SpringEmbedderGridVariant", std::make_unique<ogdf::SpringEmbedderGridVariant>()});
    
    auto ml = std::make_unique<ogdf::MultilevelLayout>();
    ml->setPlacer(new ogdf::RandomPlacer()); 
    layouts.push_back({"MultilevelLayout", std::move(ml)});

    // --- 3. Main Loop to Apply General Layouts ---
    for (const auto& entry : layouts) {
        const std::string& name = entry.first;
        ogdf::LayoutModule* layoutAlgo = entry.second.get();

        ogdf::GraphAttributes GA(G,
            ogdf::GraphAttributes::nodeGraphics |
            ogdf::GraphAttributes::edgeGraphics |
            ogdf::GraphAttributes::nodeStyle |
            ogdf::GraphAttributes::edgeStyle);

        std::uniform_real_distribution<> coord_distrib(-200.0, 200.0);
        for (ogdf::node v : G.nodes) {
            GA.x(v) = coord_distrib(gen);
            GA.y(v) = coord_distrib(gen);
            GA.width(v) = 10.0;
            GA.height(v) = 10.0;
            GA.shape(v) = ogdf::Shape::Ellipse;
        }

        std::cout << "\nApplying " << name << " layout..." << std::endl;
        try {
            layoutAlgo->call(GA);
            std::string filename = "output-" + name + ".svg";
            ogdf::GraphIO::write(GA, filename, ogdf::GraphIO::drawSVG);
            std::cout << "Saved layout to " << filename << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error applying " << name << " layout: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error applying " << name << " layout." << std::endl;
        }
    }
    
    // --- 4. Special Case: Manually Respecting Node Positions ---
    std::cout << "\n--- Special Case: Manually Respecting Node Positions ---" << std::endl;
    {
        std::string name = "NodeRespecter-Manual-KK";
        
        // 1. Set up attributes and initial positions
        ogdf::GraphAttributes GA(G, ogdf::GraphAttributes::all);
        std::uniform_real_distribution<> coord_distrib(-200.0, 200.0);
        for (ogdf::node v : G.nodes) {
            GA.x(v) = coord_distrib(gen);
            GA.y(v) = coord_distrib(gen);
            GA.width(v) = 10.0;
            GA.height(v) = 10.0;
        }

        // 2. Store the initial positions of the nodes to be fixed
        std::map<ogdf::node, ogdf::DPoint> fixedPositions;
        std::cout << "Fixing the first 10 nodes in place..." << std::endl;
        int fixedCount = 0;
        for (ogdf::node v : G.nodes) {
            if (fixedCount++ < 10) {
                fixedPositions[v] = GA.point(v);
                ogdf::Color red(255, 0, 0, 255);
                GA.fillColor(v) = red; 
            } else {
                ogdf::Color white{255, 255, 255, 255};
                 GA.fillColor(v) = white; 
            }
        }

        // 3. Run the layout algorithm
        std::cout << "Applying SpringEmbedderKK layout..." << std::endl;
        ogdf::SpringEmbedderKK kkLayout;
        kkLayout.call(GA);

        // 4. Manually reset the positions of the fixed nodes
        std::cout << "Resetting positions of fixed nodes..." << std::endl;
        for(const auto& pair : fixedPositions) {
            GA.x(pair.first) = pair.second.m_x;
            GA.y(pair.first) = pair.second.m_y;
        }

        // 5. Save the result
        std::string filename = "output-" + name + ".svg";
        ogdf::GraphIO::write(GA, filename, ogdf::GraphIO::drawSVG);
        std::cout << "Saved layout to " << filename << std::endl;
    }

    return 0;
}
#endif
