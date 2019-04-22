#include <iron/full.h>
#include <iron/gl.h>

u64 no_node = 0xFFFFFFFFFFFFFF;
typedef struct{
  u64 * indexes;
  u64 * payloads;
  u64 node_count;
}node_context;


typedef struct{
  u64 index;
  node_context * ctx;
} node;

node node_create(node_context * ctx){
  u64 index = ctx->node_count;
  ctx->node_count += 1;
  ctx->indexes = realloc(ctx->indexes, sizeof(u64) * 4 * ctx->node_count);
  for(u64 i = 0; i < 4; i++){
    ctx->indexes[i + index * 4] = no_node;
  }
  
  ctx->payloads = realloc(ctx->payloads, sizeof(u64) * ctx->node_count);
  printf("New node: %i\n", index);
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

void node_create_children(node n){
  node_context * ctx = n.ctx;
  for(u64 i = 0; i < 4; i++){
    if(ctx->indexes[n.index * 4 + i] == no_node){
      node nc = node_create(n.ctx);
      ctx->indexes[n.index * 4 + i] = nc.index;
    }
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

node * nodes;

void render_node(node nd){
  if(nd.index == no_node) return;
  u64 payload = node_get_payload(nd);
  
  colorf col = color_to_colorf(colors[payload]);
  blit_rectangle(0,0,1,1, col.rgba[0], col.rgba[1], col.rgba[2], col.rgba[3]);
  blit_scale(0.5,0.5);
  render_node(get_sub_node(nd, 0));
  blit_translate(1,0);
  render_node(get_sub_node(nd, 1));
  blit_translate(-1,1);
  render_node(get_sub_node(nd, 2));
  blit_translate(1,0);
  render_node(get_sub_node(nd, 3));
  blit_translate(-1,-1);
  blit_scale(2,2);
}

int main(){
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
  float scale = 3;
  while(true){
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

    
    render_node(first_node);
    gl_window_swap(win);
    

  }
  return 0;
}
