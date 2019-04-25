#include <iron/full.h>
#include <iron/gl.h>

#define no_node 0xFFFFFFFFFFFFFFL


typedef struct{
  u64 * indexes;
  u64 * payloads;
  u64 node_count;
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

void node_create_child(node n, size_t i){
  
  node_context * ctx = n.ctx;
  var i2 = n.index * 4 + i;
  printf("Create: %i\n", i2);
  if(ctx->indexes[i2] == no_node){
    node nc = node_create(n.ctx);
    ctx->indexes[i2] = nc.index;
  }
}

void node_create_children(node n){
  for(u64 i = 0; i < 4; i++){
    node_create_child(n, i);
  }
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

color colors[20];
u64 current_color = 1;
node detect_collision(node n, vec2 * loc){
  vec2 l = *loc;
  int child = 0;
  if(l.x > 0.5){
    l.x -= 0.5;
    child = 1;
  }
  if(l.y > 0.5){
    l.y -= 0.5;
    child += 2;
  }
  *loc = vec2_scale(l, 2.0);
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
}tree_index;

tree_index tree_index_from_data(node * nodes, u64 * child_index, size_t count){
  tree_index index = { .nodes = nodes, .childidx = child_index, .count = count, .capacity = count, .create = false};
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


void tree_index_move(tree_index * it, int rx, int ry){
  int scale = 1;
  while(true){
    if(it->count == 0) return;
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
      if(c.index == no_node)
	return;
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
    node nodes[] = { first_node, child_node2, child_node22, child_node222 };
    u64 child_index[] = {0, 2, 2, 2};
    tree_index it = tree_index_from_data(nodes, child_index, array_count(nodes));
    tree_index_move(&it, 1, 0);
    //var node = tree_index_node(&it);
    //ASSERT(node_exists(node));
    //ASSERT(node.index != child_node22.index);
    it.create = true;
    printf("---\n");
    void test_move(int x, int y){
      tree_index_move(&it, x, y);
      var node2 = tree_index_node(&it);
      printf("--->%i %i %i\n", x, y, node2.index);

    }
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, -1);
    test_move(0, 1);
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
    test_move(1,0);
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



int main(){

  test_quadtree();
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

  gl_window * win = gl_window_open(512,512);
  gl_window_make_current(win);
  bool right_drag = false;
  vec2 start_point;
  float scale = 1;
  int zoomLevel = 0;
  while(true){
    printf("Scale:%i/ %f\n", zoomLevel, scale);
    gl_window_make_current(win);
    int window_width, window_height;
    gl_window_get_size(win, &window_width, &window_height);
    gl_window_event evt_buf[10];
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
	printf("%i %i move\n", mevt->x, mevt->y);
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
    var offset = vec2_new(0,0);
    if(right_drag)
      offset = vec2_sub(mouse_pt, start_point);
    blit_translate( offset.x,  -offset.y);
    
    blit_scale(scale, scale);
    blit_translate( -0.5, -0.5);
    
    var tform = blit_get_view_transform();
    tform = mat3_invert(tform);

    vec2 p2 = mat3_mul_vec2(tform, mouse_pt);
    p2.x += 0.5;
    p2.y += 0.5;
    p2.y = 1 - p2.y;
    vec2_print(p2);vec2_print(mouse_pt);printf("\n");
    node d = first_node;
    if(click){
      while(true){
	var prev = d;
	vec2 prevp = p2;
	d = detect_collision(d, &p2);
	vec2_print(p2);
	printf(" %i\n", d.index);
	if((d.index == no_node || node_get_payload(d) == 0) && click){
	  node_create_children(prev);
	  prev = detect_collision(prev, &prevp);
	  node_set_payload(prev, current_color);
	  if(current_color != 1){
	    current_color = 1;
	  }else{
	    current_color = 2;
	  }
	  break;
	}
      }
    }

    blit_begin(BLIT_MODE_UNIT);
    blit_scale(2.0 / 512.0, 2.0 / 512.0);
    blit_translate(-256, -256);

    
    //UNUSED(nodes);
    render_node(child_node, 256,0,0);
    /*node nodes[2] = {first_node, child_node}; 
    node nodes2[10];
    int xs[10];
    int ys[10];
    get_neighbooring_nodes(nodes, nodes2,xs,ys);
    */
    gl_window_swap(win);
    

  }
  return 0;
}
