#include <curl/curl.h>
#include <iostream>
#include <limits>
#include <program.hpp>
#include <queue>
//
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/parser.h>

CURLM* multi_handle = nullptr;

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
  multi_handle = curl_multi_init();
}

Program::~Program()
{
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();
}

std::pair<std::string, std::string> Program::request_html(std::string const& url)
{
  CURL* easy = curl_easy_init();
  if(easy == nullptr) {
    throw std::runtime_error("Failed to request " + url);
  }
  std::string response{};

  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, Program::write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "Mozilla/5.0");

  curl_multi_add_handle(multi_handle, easy);

  int still_running = 0; // number of transfers still in progress
  curl_multi_perform(multi_handle, &still_running);

  while(still_running) {
    int numfds = {};
    curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
    curl_multi_perform(multi_handle, &still_running);
  }

  char* eff_url = nullptr;
  if(eff_url == nullptr) {
    throw std::runtime_error("Failed to request " + url + ": invalid effective url.");
  }

  curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
  curl_multi_remove_handle(multi_handle, easy);
  curl_easy_cleanup(easy);

  return std::make_pair(std::string(eff_url), response);
}

PageNode Program::crawl_page(std::string const& url, int max_depth)
{
  // auto node = PageNode{
  //   .url = url,
  //   .links = {},
  // };

  std::queue<PageNode> to_visit;
  // std::set<PageNode>
  // auto [final_url, html] = request_html(url);

  // return node;
  return PageNode{""};
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

    if(std::cin.fail() || depth <= 0) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      message = "invalid depth, should be a positive integer: ";
    } else {
      break;
    }
  }

  return depth;
}

static std::string resolve_url(const std::string& base_url,
  const std::string& href)
{
  std::string out;
  CURLU* h = curl_url(); // one handle is enough
  if(!h) return out;

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
  std::vector<PageNode>& out, std::string const& base_url)
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
            std::string abs = resolve_url(base_url, raw);
            if(!abs.empty()) {
              out.emplace_back(abs);
            }
          }
        }
      }
    }
    extract_links_rec(child, out, base_url);
  }
}

std::vector<PageNode> Program::parse_url(std::string url, std::string const& content)
{
  std::vector<PageNode> pages;

  auto* doc = lxb_html_document_create();
  if(!doc) return pages;

  if(lxb_html_document_parse(doc,
       reinterpret_cast<const lxb_char_t*>(content.c_str()),
       content.size()) != LXB_STATUS_OK) {
    lxb_html_document_destroy(doc);
    return pages;
  }

  if(auto* body = lxb_html_document_body_element(doc)) {
    extract_links_rec(lxb_dom_interface_node(body), pages, url);
  }

  lxb_html_document_destroy(doc);
  return pages;
}

void Program::run()
{
  print_header(); // fancy header output
  std::string root_url = request_input();
  int depth = request_depth();
  auto [effective_url, content] = request_html(root_url);
  std::vector<PageNode> links = parse_url(effective_url, content);
}
