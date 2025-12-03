use std::path::{Path, PathBuf};
use std::collections::HashMap;
use petgraph::graph::{DiGraph, NodeIndex};
use petgraph::algo::toposort;
use walkdir::WalkDir;
use sha2::{Sha256, Digest};
use std::fs;

use super::{BuildNode, EdgeType};

pub struct BuildGraph {
    graph: DiGraph<BuildNode, EdgeType>,
    node_map: HashMap<PathBuf, NodeIndex>,
}

impl BuildGraph {
    pub fn new() -> Self {
        BuildGraph {
            graph: DiGraph::new(),
            node_map: HashMap::new(),
        }
    }
    
    /// Discover source files in a directory
    pub fn discover_sources(root: &Path) -> Vec<PathBuf> {
        let mut sources = Vec::new();
        
        for entry in WalkDir::new(root)
            .follow_links(true)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            if let Some(ext) = path.extension() {
                if ext == "cpp" || ext == "cc" || ext == "cxx" {
                    sources.push(path.to_path_buf());
                }
            }
        }
        
        sources
    }
    
    /// Compute SHA256 hash of a file
    pub fn hash_file(path: &Path) -> Result<String, std::io::Error> {
        let content = fs::read(path)?;
        let mut hasher = Sha256::new();
        hasher.update(&content);
        Ok(format!("{:x}", hasher.finalize()))
    }
    
    /// Add a node to the graph
    pub fn add_node(&mut self, node: BuildNode) -> NodeIndex {
        let path = node.path().clone();
        if let Some(&idx) = self.node_map.get(&path) {
            return idx;
        }
        
        let idx = self.graph.add_node(node);
        self.node_map.insert(path, idx);
        idx
    }
    
    /// Add an edge between nodes
    pub fn add_edge(&mut self, from: NodeIndex, to: NodeIndex, edge_type: EdgeType) {
        self.graph.add_edge(from, to, edge_type);
    }
    
    /// Get topological sort of the graph
    pub fn topological_sort(&self) -> Result<Vec<NodeIndex>, String> {
        toposort(&self.graph, None)
            .map_err(|e| format!("Cycle detected in build graph: {:?}", e))
    }
    
    /// Get all nodes
    pub fn nodes(&self) -> Vec<&BuildNode> {
        self.graph.node_weights().collect()
    }
    
    /// Get node by index
    pub fn get_node(&self, idx: NodeIndex) -> Option<&BuildNode> {
        self.graph.node_weight(idx)
    }
    
    /// Get node index by path
    pub fn get_node_index(&self, path: &Path) -> Option<NodeIndex> {
        self.node_map.get(path).copied()
    }
    
    /// Get dependencies of a node
    pub fn get_dependencies(&self, idx: NodeIndex) -> Vec<NodeIndex> {
        self.graph.neighbors(idx).collect()
    }
}
