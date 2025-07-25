#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <program.hpp>
#include <unordered_set>

//
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/fileformats/GraphIO.h>
//
// Include headers for all the layout engines we will use
#include <ogdf/energybased/DavidsonHarelLayout.h>
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/GEMLayout.h>
#include <ogdf/energybased/NodeRespecterLayout.h>
#include <ogdf/energybased/PivotMDS.h>
#include <ogdf/energybased/SpringEmbedderKK.h>
#include <ogdf/energybased/StressMinimization.h>

size_t Program::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  std::string* buffer = static_cast<std::string*>(userdata);
  size_t total_size = size * nmemb; // size * number_of_bytes
  buffer->append(ptr, total_size);  // append exactly total_size bytes from ptr
  return total_size;
}

Program::Program()
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  m_multi_handle = curl_multi_init();
}

Program::~Program()
{
  curl_multi_cleanup(m_multi_handle);
  curl_global_cleanup();
}

Program::Response Program::request_html(std::string const& url)
{
  CURL* easy = curl_easy_init();
  if(!easy) {
    throw std::runtime_error("Failed to init curl for " + url);
  }

  std::string response;

  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);       // max 10 seconds per request
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L); // max 5 seconds to connect
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, Program::write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "Mozilla/5.0");

  curl_multi_add_handle(m_multi_handle, easy);

  int still_running = 0;
  curl_multi_perform(m_multi_handle, &still_running);

  while(still_running) {
    int numfds = 0;
    CURLMcode mc = curl_multi_wait(m_multi_handle, nullptr, 0, 1000, &numfds);
    if(mc != CURLM_OK) {
      curl_multi_remove_handle(m_multi_handle, easy);
      curl_easy_cleanup(easy);
      throw std::runtime_error("curl_multi_wait error");
    }
    curl_multi_perform(m_multi_handle, &still_running);
  }

  long response_code = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
  if(response_code >= 400) {
    curl_multi_remove_handle(m_multi_handle, easy);
    curl_easy_cleanup(easy);
    throw std::runtime_error("HTTP error " + std::to_string(response_code) + " for " + url);
  }

  std::optional<std::string> final_url = get_effective_url(url);
  if(!final_url) {
    throw std::runtime_error("Failed to get effective_url.");
  }

  curl_multi_remove_handle(m_multi_handle, easy);
  curl_easy_cleanup(easy);
  return std::make_pair(final_url.value(), response);
}

void Program::crawl_page(std::string const& url, int depth)
{
  std::optional<std::string> effective_url = get_effective_url(url);
  if(!effective_url) {
    throw std::runtime_error("Faield to get effective url in crawl_page.");
  }

  int index = add_node(effective_url.value(), depth);
  crawl_page_rec(get_node(index), depth);
}

std::optional<std::string> Program::get_effective_url(std::string const& url)
{
  CURL* curl = curl_easy_init();
  if(!curl) {
    return std::nullopt;
  }

  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);        // max 10 seconds per request
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L); // max 5 seconds to connect

  CURLcode res = curl_easy_perform(curl);
  char* effective_url = nullptr;
  if(res != CURLE_OK) {
    std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
    return std::nullopt;
  }
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
  if(effective_url) {
    std::string final_url = effective_url;
    if(!is_valid_url(final_url)) {
      throw std::runtime_error("invalid final url for " + url + ": " + final_url);
    }

    curl_easy_cleanup(curl);
    return std::make_optional(final_url);
  }
  return std::nullopt;
}

std::optional<PageNode::Index> Program::crawl_page_rec(PageNode& page, int depth)
{
  URL const& url = get_url(page.index());

  // we've gone far enough
  if(depth == 0) {
    return std::nullopt;
  }

  std::cout << "crawling page " << url << std::endl;
  std::string final_url{};
  std::string content{};

  try {
    std::tie(final_url, content) = request_html(url);
  }
  catch(const std::exception& e) {
    std::cerr << "Error fetching " << url << ": " << e.what() << '\n';
    return std::nullopt;
  }

  // Building blocks
  std::unordered_set<URL> children = parse_url(final_url, content);
  get_node(page.index()).reserve(children.size());

  // children
  int ended = 0;
  for(URL const& child_url : children) {
    // avoids crawling the same page twice
    if(exists(child_url)) {
      std::cout << "duplicate " << child_url << std::endl;
      return std::nullopt;
    }

    int child_index = add_node(child_url, depth);
    page.add_link(child_index);
    auto maybe_index = crawl_page_rec(get_node(child_index), depth - 1);
    if(!maybe_index && depth == 0) {
      ++ended;
    }
  }

  if(ended != 0) {
    std::cout << ended << " have reached end for " << final_url << std::endl;
  }

  return page.index();
}

void Program::print_header()
{
  std::cout << "web crawler" << std::endl;
}

std::string Program::request_input()
{
  CURLU* parser = curl_url(); // url parser

  std::string output = "type your root url: ";
  std::string url;
  while(true) {
    std::cout << output;
    std::cin >> url;

    CURLUcode code = curl_url_set(parser, CURLUPART_URL, url.c_str(), 0);
    if(code == CURLUE_OK) {
      break;
    } else {
      output = "invalid url, try again: ";
    }
  }
  curl_url_cleanup(parser);
  return url;
}

int Program::request_depth()
{
  int depth = {};
  std::string message = "depth: ";

  while(true) {
    std::cout << message;
    std::cin >> depth;

    if(std::cin.fail() || depth < 0) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      message = "invalid depth, should be a positive integer: ";
    } else {
      break;
    }
  }

  return depth;
}

std::optional<std::string> Program::resolve_url(const std::string& base_url,
  const std::string& href)
{
  std::optional<std::string> out;
  CURLU* h = curl_url(); // one handle is enough
  if(!h) {
    throw std::runtime_error("Failed to resolve url " + base_url);
  }

  do {
    // 1) set base
    if(curl_url_set(h, CURLUPART_URL, base_url.c_str(), 0) != CURLUE_OK)
      break;

    // 2) set href allowing relative URLs
    if(curl_url_set(h, CURLUPART_URL, href.c_str(),
         CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK)
      break;

    // 3) extract the resolved absolute URL
    char* full = nullptr;
    if(curl_url_get(h, CURLUPART_URL, &full, 0) == CURLUE_OK && full) {
      out = full;
      curl_free(full);
    }
  } while(false);

  curl_url_cleanup(h);
  return out;
}

// forward‑declare a small recursive helper
void Program::extract_links_rec(lxb_dom_node_t* node,
  std::unordered_set<URL>& out, std::string const& base_url)
{
  for(lxb_dom_node_t* child = node->first_child; child; child = child->next) {
    if(child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
      auto* el = lxb_dom_interface_element(child);

      // get the tag name as a C‑string
      const lxb_char_t* tag = lxb_dom_element_qualified_name(el, nullptr);
      if(tag && strcmp((const char*)tag, "a") == 0) {
        if(auto* attr = lxb_dom_element_attr_by_name(
             el, (const lxb_char_t*)"href", 4)) {
          if(auto* href = lxb_dom_attr_value(attr, nullptr)) {
            std::string raw((const char*)href);
            std::optional<Program::URL> url = resolve_url(base_url, raw);
            if(url) {
              out.insert(url.value());
            }
          }
        }
      }
    }
    extract_links_rec(child, out, base_url);
  }
}

bool Program::is_valid_url(std::string url)
{
  CURLU* h = curl_url();
  if(!h) return false;

  if(curl_url_set(h, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
    curl_url_cleanup(h);
    return false;
  }

  char* scheme = nullptr;
  if(curl_url_get(h, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK) {
    curl_url_cleanup(h);
    return false;
  }

  bool is_http = strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0;
  curl_free(scheme);
  curl_url_cleanup(h);
  return is_http;
}

std::unordered_set<Program::URL> Program::parse_url(std::string url, std::string const& content)
{
  std::unordered_set<URL> pages;
  std::string error_message = "Failed to parse " + url + ". ";

  auto* doc = lxb_html_document_create();
  if(!doc) {
    throw std::runtime_error(error_message + "lxb_html_document_create() error.");
  }

  auto parse_result = lxb_html_document_parse(doc, reinterpret_cast<const lxb_char_t*>(content.c_str()), content.size());
  if(parse_result != LXB_STATUS_OK) {
    lxb_html_document_destroy(doc);
    throw std::runtime_error(error_message + "lxb_html_document_parse() error.");
  }

  auto* body = lxb_html_document_body_element(doc);
  if(body == nullptr) {
    throw std::runtime_error(error_message + "lxb_html_document_parse() error.");
  }

  extract_links_rec(lxb_dom_interface_node(body), pages, url);
  if(pages.empty()) {
    std::cerr << error_message << "failed to extract links." << std::endl;
  }

  lxb_html_document_destroy(doc);
  return pages;
}

auto Program::get_node(PageNode::Index index) -> PageNode&
{
  return m_nodes.at(index);
}

auto Program::exists(std::string const& url) -> bool
{
  return m_url_to_index.find(url) != m_url_to_index.end();
}

auto Program::add_node(std::string const& url, int depth) -> PageNode::Index
{
  auto it = m_url_to_index.find(url);
  if(it == m_url_to_index.end()) {
    int index = m_nodes.size();
    m_index_to_url[index] = url;
    m_url_to_index[url] = index;
    m_nodes.push_back(PageNode(index, depth));

    return index;
  }

  return it->second;
}

ogdf::Color getHeatMapColor(float value)
{
  // Clamp value to the [0, 1] range to prevent errors
  value = std::max(0.0f, std::min(1.0f, value));

  // Define the key colors for the gradient
  const int numColors = 4;
  ogdf::Color colors[numColors] = {
    ogdf::Color("#4488FF"), // 1. Blue for low values
    ogdf::Color("#00FF00"), // 2. Green
    ogdf::Color("#FFFF00"), // 3. Yellow
    ogdf::Color("#FF0000")  // 4. Red for high values
  };

  // If the value is at the maximum, return the last color directly
  if(value >= 1.0f) {
    return colors[numColors - 1];
  }

  // Determine which two colors to interpolate between
  float scaledValue = value * (numColors - 1);
  int idx1 = static_cast<int>(scaledValue);
  int idx2 = idx1 + 1;
  float fraction = scaledValue - idx1;

  // Linearly interpolate the RGB components
  unsigned char r = static_cast<unsigned char>(colors[idx1].red() * (1 - fraction) + colors[idx2].red() * fraction);
  unsigned char g = static_cast<unsigned char>(colors[idx1].green() * (1 - fraction) + colors[idx2].green() * fraction);
  unsigned char b = static_cast<unsigned char>(colors[idx1].blue() * (1 - fraction) + colors[idx2].blue() * fraction);

  return ogdf::Color(r, g, b);
}


/**
 * @brief Generates and styles a graph based on web crawler data.
 *
 * This function creates a graph visualization from crawled web pages, styling nodes
 * and edges based on their connectivity. Nodes with more links are larger and colored
 * more intensely on a heatmap. Edges are styled to match.
 */
void Program::graph()
{
  // 1. Graph Construction
  // ---------------------
  ogdf::Graph G;

  std::vector<ogdf::node> nodes;
  nodes.reserve(m_nodes.size());
  for(size_t i = 0; i < m_nodes.size(); ++i) {
    nodes.push_back(G.newNode());
  }

  for(std::size_t i = 0; i < m_nodes.size(); ++i) {
    PageNode const& node = m_nodes[i];
    for(int child_index : node.children()) {
      if(child_index >= 0 && child_index < static_cast<int>(nodes.size())) {
        G.newEdge(nodes[i], nodes[child_index]);
      }
    }
  }

  std::cout << "[graph] Nodes: " << G.numberOfNodes()
            << ", Edges: " << G.numberOfEdges() << "\n";

  if(G.numberOfNodes() == 0) {
    std::cout << "Graph is empty, skipping layout." << std::endl;
    return;
  }

  // 2. Style Calculation
  // --------------------
  int maxDegree = 0;
  for(ogdf::node v : G.nodes) {
    if(v->degree() > maxDegree) {
      maxDegree = v->degree();
    }
  }
  if(maxDegree == 0) maxDegree = 1;

  // 3. Attribute Assignment
  // -----------------------
  ogdf::GraphAttributes GA(G, ogdf::GraphAttributes::all);

  // --- NEW: Pre-computation step to find parents efficiently ---
  // Since we can't ask a node for its parent directly, we build a map.
  // The key is the child node, and the value is its parent node.
  std::unordered_map<ogdf::node, ogdf::node> childToParentMap;
  for(ogdf::edge e : G.edges) {
    // The target of a directed edge is the child.
    childToParentMap[e->target()] = e->source();
  }

  // --- MODIFIED: Style nodes based on their PARENT's degree ---
  const double minNodeSize = 0.75;
  const double maxNodeSize = 2.50;
  for(ogdf::node v : G.nodes) {
    double size = maxNodeSize; // Default to max size (for root nodes)

    // Find the parent in our pre-computed map
    auto it = childToParentMap.find(v);
    if(it != childToParentMap.end()) {
      // If a parent is found...
      ogdf::node parent = it->second;

      // Normalize the parent's degree
      double normalizedParentDegree = static_cast<double>(parent->degree()) / maxDegree;

      // Calculate size to be INVERSELY proportional to the parent's degree
      size = minNodeSize + (maxNodeSize - minNodeSize) * (1.0 - normalizedParentDegree);
    }

    GA.width(v) = size;
    GA.height(v) = size;
    GA.shape(v) = ogdf::Shape::Ellipse;

    // Color is still based on the node's OWN degree
    double normalizedDegree = static_cast<double>(v->degree()) / maxDegree;
    GA.fillColor(v) = getHeatMapColor(normalizedDegree);
    GA.strokeColor(v) = ogdf::Color("#000000");
    GA.strokeWidth(v) = 0.05;
  }


  // Style edges (unchanged)
  for(ogdf::edge e : G.edges) {
    int max_end_degree = std::max(e->source()->degree(), e->target()->degree());
    double norm_val = static_cast<double>(max_end_degree) / maxDegree;

    ogdf::Color tempColor = getHeatMapColor(norm_val);
    ogdf::Color edgeColor(tempColor.red(), tempColor.green(), tempColor.blue(), uint8_t{120});
    GA.strokeColor(e) = edgeColor;

    // double width = 0.2 + 2.0 * std::pow(norm_val, 2.0);
    // GA.strokeWidth(e) = width;
    GA.strokeWidth(e) = 0.05;

    GA.arrowType(e) = ogdf::EdgeArrow::None;
  }

  // 4. Layout and Export
  // --------------------
  // ogdf::FMMMLayout lay;
  // ogdf::SpringEmbedderKK lay;
  // ogdf::GEMLayout lay;
  // ogdf::DavidsonHarelLayout lay;
  // ogdf::PivotMDS lay;
  // ogdf::StressMinimization lay;
  // ogdf::NodeRespecterLayout lay;

  auto runLayout = [&](ogdf::LayoutModule& layout, const std::string& name) {
    std::cout << "Running " << name << " layout..." << std::endl;
    // We make a copy of the attributes to ensure each layout starts fresh
    ogdf::GraphAttributes GAcopy = GA;
    layout.call(GAcopy);
    std::string filename = "graph-" + name + ".svg";
    ogdf::GraphIO::write(GAcopy, filename, ogdf::GraphIO::drawSVG);
    std::cout << "Wrote " << filename << std::endl;
  };

  // Engine 1: FMMMLayout (Fast Multipole Multilevel Method)
  ogdf::FMMMLayout fmmmLayout;
  fmmmLayout.useHighLevelOptions(true);
  fmmmLayout.unitEdgeLength(15.0);
  fmmmLayout.newInitialPlacement(true);
  fmmmLayout.qualityVersusSpeed(ogdf::FMMMOptions::QualityVsSpeed::GorgeousAndEfficient);
  runLayout(fmmmLayout, "FMMMLayout");

  // Engine 2: StressMinimization
  ogdf::StressMinimization stressMinimization;
  runLayout(stressMinimization, "StressMinimization");

  // Engine 3: NodeRespecterLayout
  ogdf::NodeRespecterLayout nodeRespecterLayout;
  runLayout(nodeRespecterLayout, "NodeRespecterLayout");
}

void Program::run()
{
  print_header(); // fancy header output
  // std::string root_url = request_input();
  // int depth = request_depth();

  std::string root_url = "https://en.cppreference.com/w/";
  int depth = 3;

  std::cout << "Crawling pages from root " << root_url << std::endl;

  crawl_page(root_url, depth);

  std::cout << "Done! node_count: " << m_nodes.size() << std::endl;
  std::cout << "index_to_url " << m_index_to_url.size() << std::endl;
  std::cout << "url_to_index " << m_url_to_index.size() << std::endl;
  std::cout << "m_nodes      " << m_nodes.size() << std::endl;

  graph();
}
