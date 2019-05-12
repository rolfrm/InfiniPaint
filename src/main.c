#include <iron/full.h>
#include <iron/gl.h>

#define no_node 0xFFFFFFFFFFFFFFL


typedef struct{
  u64 * indexes;
  u64 * payloads;
  u64 node_count;
  u64 last_parent_index;
}node_context;

typedef struct{
  u64 index;
  node_context * ctx;
} node;

node no_node_node = {.index = no_node, .ctx = NULL};

bool node_exists(node nd){
  return nd.index != no_node;
}

node node_create(node_context * ctx){
  
  u64 index = ctx->node_count;
  ctx->node_count += 1;
  ctx->indexes = realloc(ctx->indexes, sizeof(u64) * 4 * ctx->node_count);
  for(u64 i = 0; i < 4; i++){
    ctx->indexes[i + index * 4] = no_node;
  }
  
  ctx->payloads = realloc(ctx->payloads, sizeof(u64) * ctx->node_count);
  ctx->payloads[index] = 0;
  return (node){.ctx = ctx, .index = index};
}

node_context * node_context_create(){
  node_context * ctx = alloc0(sizeof(*ctx));
  return ctx;
}

node get_sub_node(node n, int child_nr){
  node_context * ctx = n.ctx;
  ASSERT(child_nr >= 0);
  ASSERT(child_nr < 4);
  u64 child_index = n.index * 4 + child_nr;
  if(ctx->node_count < n.index){
    return (node){.ctx = ctx, .index = no_node};
  }
  u64 newindex = ctx->indexes[child_index];
  return (node){.ctx = ctx, .index = newindex};
}

node node_create_child(node n, size_t i){
  node_context * ctx = n.ctx;
  var i2 = n.index * 4 + i;
  if(ctx->indexes[i2] == no_node){
    node nc = node_create(n.ctx);
    ctx->indexes[i2] = nc.index;
    return nc;
  }
  return (node){.ctx = n.ctx, .index = ctx->indexes[i2]}; 
}

void node_create_children(node n){
  for(u64 i = 0; i < 4; i++){
    node_create_child(n, i);
  }
}

node node_create_parent(node n){
  node parent = node_create(n.ctx);
  u64 child_index = n.ctx->last_parent_index;
  if(child_index == 0)
    child_index = 3;
  else
    child_index = 0;
  n.ctx->indexes[parent.index * 4 + child_index] = n.index;
  n.ctx->last_parent_index = child_index;

  return parent;
}

void node_set_payload(node n, u64 pl){
  node_context * ctx = n.ctx;
  ctx->payloads[n.index] = pl;
}

u64 node_get_payload(node n){
  node_context * ctx = n.ctx;
  return ctx->payloads[n.index];
}

typedef struct{
  union{
    u8 rgba[4];
    u32 raw;
  };
}color;

typedef struct{
  union{
    f32 rgba[4];
  };
}colorf;

colorf color_to_colorf(color col){
  colorf c;
  for(int i = 0 ; i < 4; i++){
    c.rgba[i] = col.rgba[i];
    c.rgba[i] /= 255;
  }
  return c;
}
// -0.5
// -1.0
color colors[20];
u64 current_color = 1;
node detect_collision(node n, vec2 * loc){
  vec2 l = *loc;
  int child = 0;

  if(l.x > 0.0){
    l.x -= 0.5;
    child = 1;
  }else{
    l.x += 0.5;
    child = 0;
  }
  if(l.y > 0.0){
    l.y -= 0.5;
    child += 2;
  }else{
    l.y += 0.5;
  }
  l = vec2_scale(l, 2.0);
  *loc = l;
  return get_sub_node(n, child);
}
typedef struct{
  int width, height;
  
}render_context;

typedef struct{
  u64 * childidx;
  node * nodes;
  size_t count;
  size_t capacity;
  bool create;
  bool hit_parent;
}tree_index;

tree_index tree_index_from_data(node * nodes, u64 * child_index, size_t count){
  tree_index index = { .nodes = nodes, .childidx = child_index, .count = count, .capacity = count, .create = false, .hit_parent = false};
  return index;
}

void tree_index_parent(tree_index * it){
  ASSERT(it->count > 0);
  it->count--;
}

node tree_index_node(tree_index * it){
  if(it->count == 0)
    return no_node_node;
  return it->nodes[it->count - 1];
}

node tree_index_child(tree_index * it, size_t index){
  ASSERT(index < 4);
  node pnode = tree_index_node(it);
  ASSERT(node_exists(pnode));
  u64 child_index = pnode.ctx->indexes[pnode.index * 4 + index];
  if(child_index == no_node && it->create){
    node_create_child(pnode, index);
    child_index = pnode.ctx->indexes[pnode.index * 4 + index];
    ASSERT(child_index != no_node);
  }
  node child = {.ctx = it->nodes[0].ctx, .index = child_index};
  return child;
}

node tree_index_push_child(tree_index * it, int x, int y){
  ASSERT(it->count != 0);
  ASSERT(x == 0 || x == 1);
  ASSERT(y == 0 || y == 1);
  ASSERT(it->count < it->capacity);
  size_t index = x + y * 2;
  node child = tree_index_child(it, index);
  it->nodes[it->count] = child;
  it->childidx[it->count] = index; 
  it->count++;
  return child;
}

void tree_index_move2(tree_index * it, int rx, int ry){
  // in contrast to the old tree move algorithm,
  // this can move the index across a gap of any width.
  // it does not suffer from the precision issue
  // however it is incapable of moving across child nodes that does not exist.
  // for this, i'd need virtual nodes, which are essentially the same as bigints
  // an address in the tree is essentially just a big number.. 
  ASSERT(rx == 0 || ry == 0);
  ASSERT(rx != 0 || ry != 0);
  ASSERT(rx >= -1 && rx <= 1);
  ASSERT(ry >= -1 && ry <= 1);

  // rx == 1:
  // walk up the tree and search for a child that is x=0, then walk down that branch
  // rx == -1:
  // walk up the tree and search for a child that is x=1.
  // keep y the same all the way
  int mask = 0;
  int neg = 0;
  if(rx == 1)
    mask = 0b01;
  else if (rx == -1)
    mask = 0b01, neg = 0b01;
  else if(ry == 1)
    mask = 0b10;
  else if(ry == -1)
    mask = 0b10, neg=0b10;
  int counter = -1;
  // go up and find the first parent that satisfies condition.
  
  for(int i = 0; i < (int)it->count -1; i++){
      int mem_index = it->count - 1 -i;
      int cid = it->childidx[mem_index];
      
      bool found_parent = (bool)(neg == (mask & cid));
      if(found_parent){
	counter = i;
	break;
      }
  }
  if(counter < 0){
    it->hit_parent = true;
    return;
  }
  
  // does this work?
    for(int i = counter; i >= 0; i--){
      int mem_index = it->count - 1 -i;
      int cid = it->childidx[mem_index];
      if(neg){
	if(i != counter){
	  cid = cid | mask;
	}else{
	  cid = cid & ~mask;
	}
      }else{
	if(i == counter){
	  cid = cid | mask;
	}else{
	  cid = cid & ~mask;
	}
      }

      if(mem_index == 0){
	it->hit_parent = true;
	return;
      }

      node cnode = node_create_child(it->nodes[mem_index - 1], cid);

      it->nodes[mem_index] = cnode;
      it->childidx[mem_index] = cid;
    }
}


void tree_index_move(tree_index * it, int rx, int ry){

  int scale = 1;
  while(true){
    
    if(it->count == 0){
      it->hit_parent = true;
      return;
    }
    
    if(rx == 0 && ry == 0 && scale == 1)
      return;
    int cid = it->childidx[it->count - 1];
    if(rx < scale && ry < scale &&  rx >= 0 && ry >= 0 ){
      ASSERT(scale > 1);
      // move to child.
      int newscale = scale / 2;
      int q1 = rx >= newscale;
      int q2 = ry >= newscale;
      rx -= q1 * newscale;
      ry -= q2 * newscale;
      scale = newscale;
      node c = tree_index_push_child(it, q1, q2);
      if(c.index == no_node){
	return;
      }
    }else{
      int cx = cid % 2;
      int cy = (cid / 2)  % 2;
      rx += cx * scale;
      ry += cy * scale;
      scale *= 2;
      tree_index_parent(it);
    }
  }
}

/*
typedef struct{
  union {
    struct{
      bool base:1;
      i64 number:63:
  
    };
    void * extension;
  };
}bigint;

typedef struct{
  bool extended;
  bigint numerator;
  bigint denominator;
}arbf;
*/

// fixing the precision issue:
// instead of trying to render everything, have a focused node and lookout neighbooring nodes.
// this way I can keep using integer precision.

void render_node(node nd, int size, int x, int y){
  if(nd.index == no_node) return;
  u64 payload = node_get_payload(nd);
  
  colorf col = color_to_colorf(colors[payload]);
  blit_rectangle(x,y,size,size, col.rgba[0], col.rgba[1], col.rgba[2], col.rgba[3]);
  size /= 2;
  if(size == 0) return;
  render_node(get_sub_node(nd, 0), size, x, y);
  render_node(get_sub_node(nd, 1), size, x + size, y);
  render_node(get_sub_node(nd, 2), size, x, y + size);
  render_node(get_sub_node(nd, 3), size, x + size, y + size);
}

typedef struct{
  node * nodes; //roots to parent.
  u64 * child_index;
  u64 count;
  node_context * ctx;
}tree_context;

tree_context * tree_context_new(){
  node_context * nctx = node_context_create();
  node_create(nctx); // discard node.
  node first_node = node_create(nctx);

  tree_context * ctx = alloc0(sizeof(tree_context));
  
  ctx->ctx = nctx;
  ctx->nodes = alloc0(sizeof(ctx->nodes[0]));
  ctx->nodes[0] = first_node;
  ctx->child_index = alloc0(sizeof(ctx->child_index[0]));
  ctx->child_index[0] = 0;

  ctx->count = 1;
  return ctx;
}

void tree_context_add_parent(tree_context * ctx){
  u64 new_count = ctx->count + 1;
  ctx->nodes = realloc(ctx->nodes, sizeof(ctx->nodes[0]) * new_count);
  ctx->child_index = realloc(ctx->child_index, sizeof(ctx->child_index) * new_count);
  node new_parent = node_create_parent(ctx->nodes[ctx->count - 1]);
  ctx->nodes[ctx->count] = new_parent;
  ctx->child_index[ctx->count - 1] = ctx->ctx->last_parent_index;
  ctx->child_index[ctx->count] = 0;
  ctx->count = new_count;
}

typedef struct{
  node * nodes;
  u64 * child_index;
  u64 count;
  u64 init_count;
  tree_context * ctx;
  tree_index index;
}tree_it;
void tree_it_move(tree_it * it, int x, int y);
tree_it * tree_it_new(tree_context * ctx){
  tree_it * it = alloc0(sizeof(tree_it));
  it->ctx = ctx;
  tree_it_move(it, 0, 0);
  
  return it;
}

void tree_it_delete(tree_it ** iterator){
  dealloc(*iterator);
  *iterator = NULL; 
}


void * realloc_copy_rest(void * array, void * src, size_t s1, size_t s){
  array = realloc(array, s);
  memcpy(array + s1, src + s1, s - s1);
  return array;
}

void array_reverse(void * array, size_t elem_size, size_t count){
  if(array == NULL || count == 0) return;
  char buffer[elem_size];
  for(size_t i = 0; i < count / 2; i++){
    size_t i2 = count - i - 1;
    void * buf = buffer;
    void * loc1 = array + elem_size * i;
    void * loc2 = array + elem_size * i2;
    memcpy(buf, loc1, elem_size);
    memcpy(loc1, loc2 , elem_size);
    memcpy(loc2, buf, elem_size);
  }
}

void tree_it_move(tree_it * it, int x, int y){
  
 start:;

  u64 node_count = it->ctx->count;
  if(it->nodes == NULL || it->count != node_count){
  
    array_reverse(it->nodes, sizeof(it->nodes[0]), it->init_count);
    array_reverse(it->child_index, sizeof(it->child_index[0]), it->init_count);
    it->nodes = realloc(it->nodes, node_count * sizeof(it->nodes[0]));
    memcpy(it->nodes + it->init_count, it->ctx->nodes + it->init_count, (node_count - it->init_count) * sizeof(it->nodes[0]));

    it->child_index = realloc(it->child_index, node_count * sizeof(it->child_index[0]));

    memcpy(it->child_index + it->init_count -1, it->ctx->child_index + it->init_count - 1, (node_count - it->init_count + 1) * sizeof(it->child_index[0]));
    
    array_reverse(it->nodes, sizeof(it->nodes[0]), node_count);
    array_reverse(it->child_index, sizeof(it->child_index[0]), node_count);
    
    it->count = node_count;
    it->init_count = node_count;
    it->index = tree_index_from_data(it->nodes, it->child_index, node_count);
  }
  if(x == 0 && y == 0) return;

  tree_index_move2(&it->index, x, y);

  
  if(it->index.hit_parent){
    it->index.hit_parent = false;
    tree_context_add_parent(it->ctx);
    goto start;
  }
}

node tree_it_node(tree_it * it){
  if(it->nodes == NULL){
    tree_it_move(it, 0, 0);
  }
  return tree_index_node(&it->index);
}

/*
void render_tree(node * nodes, int node_count, int x, int y, int size, int width, int height){
  // x and y are offsets for the rendering
  // size is the size of the node level rendering.
  // width and height are the size of the screen.
  if(size < width || size < height){
    render_tree(n
    return;
  }
  }*/

/*void get_neighbooring_nodes(node * node_tree, int node_count, node * out_nodes, int *xs, int *ys){
  node leaf = node_tree[node_count - 1];
  
  
  }*/
/*
node get_relative_node(node * node_tree, int node_count, int x, int y){
  if(node_count == 0) return no_node_node;
  if(x == 0 && y == 0)
    return node_tree[node_count - 1];
  if(node_count == 1) return no_node_node;
  int child_index = 
  }*/

void test_quadtree(){
  node_context * ctx = node_context_create();
  node first_node = node_create(ctx);
  node_create_children(first_node);
  node child_node = get_sub_node(first_node, 1);
  node child_node2 = get_sub_node(first_node, 2);
  node child_node3 = get_sub_node(first_node, 3);
  node_set_payload(child_node3, 15);
  
  for(int j = 0; j < 4; j++){
    node rn = child_node;
    u64 rnp = 6;
    u64 other = 10;
    for(u64 i = 0; i < 8; i++){
      
      node_create_children(rn);
      rn = get_sub_node(rn, j);
      ASSERT(rn.index != no_node);
      node_set_payload(rn, rnp);
      SWAP(rnp, other);
    }
  }
  node_create_children(child_node2);
  node child_node22 = get_sub_node(child_node2, 2);
  
  node_set_payload(child_node22, 5);
  node_set_payload(child_node, 5);
  node_set_payload(child_node2, 10);
  ASSERT(child_node.index != no_node);
  ASSERT(child_node2.index != no_node);
  ASSERT(child_node2.index != child_node.index);
  ASSERT(node_get_payload(child_node) == 5);
  ASSERT(node_get_payload(child_node2) == 10);

  {
    node nodes[2] = { first_node, child_node };
    u64 child_index[2] = {0, 1};
    tree_index it = tree_index_from_data(nodes, child_index, array_count(nodes));
    tree_index_move(&it, -1, 1);
    node nd = tree_index_node(&it);
    ASSERT(node_get_payload(nd) == 10);

    tree_index_move(&it, 1, 0);
    node nd2 = tree_index_node(&it);
    ASSERT(node_get_payload(nd2) == 15);

    tree_index_move(&it, 1, 0);
    node nd3 = tree_index_node(&it);
    ASSERT(node_exists(nd3) == false);
  }
  {
    
    node child_node222 = get_sub_node(child_node22, 2);
    
    u64 * child_index;
    node * nodes;
    size_t node_count = 4;
    {
      node _nodes[] = { first_node, child_node2, child_node22, child_node222 };
      nodes = iron_clone(_nodes, sizeof(_nodes));
      u64 _child_index[] = {0, 2, 2, 2};
      child_index = iron_clone(_child_index, sizeof(_child_index));
    }
    
    
    
    UNUSED(nodes);UNUSED(child_index);
    tree_index it = tree_index_from_data(nodes, child_index, node_count);
    tree_index_move(&it, 1, 0);
    //var node = tree_index_node(&it);
    //ASSERT(node_exists(node));
    //ASSERT(node.index != child_node22.index);
    it.create = true;
    printf("---\n");
    

    void test_move(int x, int y){
      tree_index_move2(&it, x, y);
      if(it.hit_parent){
	node parent = node_create_parent(nodes[0]);
	it.capacity += 1;
	node_count += 1;
	nodes = realloc(nodes, sizeof(nodes[0]) * node_count);
	child_index = realloc(child_index, sizeof(child_index[0]) * node_count);
	nodes[0] = parent;
	child_index[0] = 0;
	memmove(nodes + 1, nodes, (node_count - 1) * sizeof(nodes[0]));
	memmove(child_index + 1, child_index, (node_count - 1) * sizeof(child_index[0]));
	child_index[1] = ctx->last_parent_index;
	bool do_create = it.create;
	it = tree_index_from_data(nodes, child_index, node_count);
	it.create = do_create;
	test_move(x, y);
	return;
      }
    }
    
    /*
    for(int i = 0; i < 1000; i++)
      test_move(0, -1000000);
    for(int i = 0; i < 100; i++)
      test_move(0, 1);
    */

    {
      test_move(1,0);
      test_move(-1,0);
 
      var node = tree_index_node(&it);
      printf("Got node3: %i\n", node.index);
    }
    
    for(int i = 0; i < 7; i++){
      test_move(-1, 0);
      var node = tree_index_node(&it);
      printf("Got node2: %i\n", node.index);
    }
    
    for(int j = 0; j < 2; j++){
      printf("\n");
      for(int i = 0; i < 7; i++){
	test_move(1, 0);
	var node = tree_index_node(&it);
	printf("Got node: %i\n", node.index);
      }
      for(int i = 0; i < 7; i++){
	test_move(-1, 0);
	var node = tree_index_node(&it);
	printf("Got node4: %i\n", node.index);
      }
    }

    printf("---------\n");
    test_move(0,1);
    test_move(0,-1);
 
    for(int i = 0; i < 7; i++){
      test_move(0, -1);
      var node = tree_index_node(&it);
      printf("Got node2: %i\n", node.index);
    }
    
    for(int j = 0; j < 2; j++){
      printf("\n");
      for(int i = 0; i < 7; i++){
	test_move(0, 1);
	var node = tree_index_node(&it);
	printf("Got node: %i\n", node.index);
      }
      for(int i = 0; i < 7; i++){
	test_move(0, -1);
	var node = tree_index_node(&it);
	printf("Got node4: %i\n", node.index);
      }
    }
    for(int i = 0; i < 70; i++){
	test_move(0, 1);
	var node = tree_index_node(&it);
	printf("Got node5: %i\n", node.index);
      }
 
    
    /*test_move(0, 1);
    test_move(0, 1);
    test_move(0, 1);
    test_move(0, 1);
    test_move(0, 1);
    test_move(0, 1);
    test_move(0, 1);
    test_move(-1,0);
    test_move(1,0);
    test_move(1,0);
    test_move(1,0);
    test_move(1,0);
    test_move(1,0);
    test_move(1,0);
    test_move(1,0);*/
    //test_move(0, 1);
    /*var node2 = tree_index_node(&it);
    ASSERT(node_exists(node2));

    it.create = true;
    
    tree_index_move(&it, -2, 0);
    var node3 = tree_index_node(&it);
    ASSERT(node_exists(node3) == false);
    */
  }
  
}

void test_tree_context(){

  for(int i = 1; i < 10; i++){
    i32 array[i];
    i32 array2[i];
    for(int j = 0; j < i; j++){
      array[j] = j;
      array2[i - j - 1] = j;
    }
    array_reverse(array, sizeof(array[0]), array_count(array));
    for(size_t i = 0; i < array_count(array); i++){
      ASSERT(array[i] == array2[i]);
    }
  }

  tree_context * tctx = tree_context_new();
  tree_it * it_ctx = tree_it_new(tctx);
  void test_move(int x, int y){
    /*printf("Move: \n");
    for(u64 i = 0; i < it_ctx->count; i++){
      printf("%i %i\n", it_ctx->nodes[i].index, it_ctx->child_index[i]);
    }
    printf("\n");
    */
    tree_it_move(it_ctx, x, y);
    node n = tree_it_node(it_ctx);
    printf("node> %i\n", n.index);
  }
  UNUSED(it_ctx);
  test_move( 0, 0);
  test_move( 1, 0);
  test_move( -1, 0);
  test_move( 1, 0);
  test_move( -1, 0);
  test_move( 1, 0);
  test_move( 1, 0);
  test_move( -1, 0);
  test_move( -1, 0);
  for(int i = 0; i < 10000; i++)
    test_move( -1, 0);
  for(int i = 0; i < 20000; i++)
    test_move( 1, 0);
  for(int i = 0; i < 10000; i++)
    test_move( -1, 0);
  for(int i = 0; i < 10000; i++)
    test_move( 0, -1);
  for(int i = 0; i < 20000; i++)
    test_move( 0, 1);
  for(int i = 0; i < 10000; i++)
    test_move( 0, -1);
}


int main(){

  test_quadtree();
  test_tree_context();
  return 0;
  colors[5].raw = 0xFFFFFFFF;
  colors[10].raw = 0xDDDDDDDD;
  colors[6].raw = 0xBBBBBBBB;
  colors[2].raw = 0xFF0000FF;
  colors[1].raw = 0xFFFF00FF;

  
  
  node_context * ctx = node_context_create();
  node first_node = node_create(ctx);
  node_create_children(first_node);
  node child_node = get_sub_node(first_node, 1);
  node child_node2 = get_sub_node(first_node, 2);

  for(int j = 0; j < 4; j++){
    node rn = child_node;
    u64 rnp = 6;
    u64 other = 10;
    for(u64 i = 0; i < 8; i++){
      
      node_create_children(rn);
      rn = get_sub_node(rn, j);
      ASSERT(rn.index != no_node);
      node_set_payload(rn, rnp);
      SWAP(rnp, other);
    }
  }
  node_create_children(child_node2);
  node child_node22 = get_sub_node(child_node2, 2);
  
  node_set_payload(child_node22, 5);
  node_set_payload(child_node, 5);
  node_set_payload(child_node2, 10);
  ASSERT(child_node.index != no_node);
  ASSERT(child_node2.index != no_node);
  ASSERT(child_node2.index != child_node.index);
  ASSERT(node_get_payload(child_node) == 5);
  ASSERT(node_get_payload(child_node2) == 10);


  node child_node222 = get_sub_node(child_node22, 2);
    
    u64 * child_index;
    node * nodes;
    size_t node_count = 4;
    {
      node _nodes[] = { first_node, child_node2, child_node22, child_node222 };
      nodes = iron_clone(_nodes, sizeof(_nodes));
      u64 _child_index[] = {0, 2, 2, 2};
      child_index = iron_clone(_child_index, sizeof(_child_index));
    }
    
    
    
    UNUSED(nodes);UNUSED(child_index);
    tree_index it = tree_index_from_data(nodes, child_index, node_count);
    tree_index_move(&it, 1, 0);
    //var node = tree_index_node(&it);
    //ASSERT(node_exists(node));
    //ASSERT(node.index != child_node22.index);
    it.create = true;
    printf("---\n");

    
    void test_move(int x, int y, u64 paint){
      
      tree_index_move2(&it, x, y);
      if(it.hit_parent){
	node parent = node_create_parent(nodes[0]);
	it.capacity += 1;
	node_count += 1;
	nodes = realloc(nodes, sizeof(nodes[0]) * node_count);
	child_index = realloc(child_index, sizeof(child_index[0]) * node_count);
	nodes[0] = parent;
	child_index[0] = 0;
	memmove(nodes + 1, nodes, (node_count - 1) * sizeof(nodes[0]));
	memmove(child_index + 1, child_index, (node_count - 1) * sizeof(child_index[0]));
	child_index[1] = ctx->last_parent_index;
	bool do_create = it.create;
	it = tree_index_from_data(nodes, child_index, node_count);
	it.create = do_create;
	test_move(x, y, paint);
	
	return;
      }
      if(paint != 0){
	var node = tree_index_node(&it);
	node_set_payload(node, 1);
      }
    }

    for(int i = 0; i < 32; i++){
      test_move(1,0, 0);
    }
    for(int i = 0; i < 32; i++){
      test_move(-1,0, 0);
    }

  
  gl_window * win = gl_window_open(512,512);
  gl_window_make_current(win);
  bool right_drag = false;
  vec2 start_point;
  UNUSED(start_point);
  float scale = 1;
  int zoomLevel = 0;
  vec2 offset = vec2_new(0.5,0);
  bool should_close = false;
  while(should_close == false){
    //offset.x += 0.01;

    gl_window_make_current(win);
    int window_width, window_height;
    gl_window_get_size(win, &window_width, &window_height);
    gl_window_event evt_buf[10];

    bool states[4];
    int keys[] = {KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT};
    for(u64 i = 0; i < array_count(keys); i++){
      states[i] = gl_window_get_key_state(win, keys[i]);
    }

    size_t event_cnt = gl_get_events(evt_buf, array_count(evt_buf));
    UNUSED(event_cnt);

    int mouse_x, mouse_y;
    get_mouse_position(win, &mouse_x, &mouse_y);
    vec2 mouse_pt = vec2_div(vec2_new(mouse_x, mouse_y), vec2_new(window_width, window_height));
    mouse_pt.x = mouse_pt.x * 2 - 1;
    mouse_pt.y = mouse_pt.y * 2 - 1;
    
    bool click = false;
    for(size_t i = 0; i < event_cnt; i++){
      var evt = evt_buf[i];
      if(evt.type ==EVT_WINDOW_CLOSE){
	should_close = true;
      }
      if(evt.type == EVT_MOUSE_BTN_DOWN){
	
	var btevt = (evt_mouse_btn *)&evt;
	if(btevt->button == 0){
	  click = true;
	}else{
	  start_point = mouse_pt;
	  right_drag = true;
	}
      }else if(evt.type == EVT_MOUSE_BTN_UP){
	right_drag = false;
      }else if(evt.type == EVT_MOUSE_MOVE){
	var mevt = (evt_mouse_move *) &evt;
	UNUSED(mevt);
      }else if(evt.type == EVT_MOUSE_SCROLL){
	var sevt = (evt_mouse_scroll *) &evt;
	scale *= (1 + sevt->scroll_y * 0.1);
	if(sevt->scroll_y > 0)
	  zoomLevel += 1;
	else
	  zoomLevel -= 1;

      }
    }
    
    UNUSED(right_drag);
    //printf("%i %i\n", mouse_x, mouse_y);

 
    blit_begin(BLIT_MODE_UNIT);

    blit_rectangle(-1,-1,2,2,0,0,0,1);
    if(right_drag)
      offset = vec2_add(offset, vec2_sub(mouse_pt, start_point));
    while(offset.x > 1){
      offset.x = 0;
    }
    
    /*var offset = vec2_new(0,0);

    */
      //blit_translate( offset.x,  -offset.y);
    
    blit_scale(scale, scale);
    blit_translate( -0.5, -0.5);

    void paintat(vec2 p, node d){
      printf("\n\n");
      vec2_print(p);vec2_print(mouse_pt);printf("\n");
    
      for(int i = 0; i < 6; i++){
	var prev = d;
	vec2 prevp = p;
	d = detect_collision(d, &p);
	vec2_print(p);
	//printf(" %i\n", d.index);
	if(d.index == no_node || node_get_payload(d) == 0){
	  node_create_children(prev);
	  prev = detect_collision(prev, &prevp);
	  d = prev;
	  if(i == 5){
	    node_set_payload(prev, 6);
	  }
	}
      }
    }
    
    var tform = blit_get_view_transform();
    tform = mat3_invert(tform);
    
    vec2 p2 = mat3_mul_vec2(tform, mouse_pt);
    p2.y = 0 - p2.y;
    node d = nodes[node_count - 7];
    click = gl_window_get_btn_state(win, 0) != 0;
    
    if(click){
      paintat(p2, d);
    }

    for(int i = 0; i < 4; i++){

      node * nodes_buffer2 = iron_clone(nodes, node_count * sizeof(nodes[0]));
      u64 * child_index_buffer2 = iron_clone(child_index, node_count * sizeof(child_index[0]));
      tree_index it2 = tree_index_from_data(nodes_buffer2, child_index_buffer2, node_count - 6);

      
      //render_node(nodes_buffer2[node_count - 7], 512,0,0);
      float ox = 0, oy = 0;
      if((i & 0x1) != 0)
	ox = 1.0;
      if((i & 0x2) != 0)
	oy = 1.0;
      if(i == 1)
	tree_index_move2(&it2, 1, 0);
      if(i == 2)
	tree_index_move2(&it2, 0, 1);
      if(i == 3){
	tree_index_move2(&it2, 1, 0);
	if(it2.hit_parent == true){
	  it2.hit_parent = false;
	  tree_index_move2(&it2, 0, 1);
	  tree_index_move2(&it2, 1, 0);
	}else{
	  tree_index_move2(&it2, 0, 1);
	}
      }
      //tree_index_move2(&it2, 1, 0);
      if(it2.hit_parent == false){
	blit_begin(BLIT_MODE_UNIT);
	blit_translate(-offset.x, -offset.y);
	blit_scale(2.0 / 512.0, 2.0 / 512.0);
	blit_translate(-256 + ox * 512, -256 + oy * 512);      
	render_node(tree_index_node(&it2), 512,0,0);
      }
      dealloc(nodes_buffer2);
      dealloc(child_index_buffer2);

    }
    
    int y_move = states[0] - states[1];
    int x_move = states[2] - states[3];
    
    if(x_move != 0){
      test_move(x_move,0, 6);
    }
    if(y_move != 0){
      test_move(0, y_move, 6);
    }
    
    gl_window_swap(win);
  }
  
  return 0;
}
